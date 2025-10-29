#include "yolov8_person_plugin.h"
#include "r_utils/r_logger.h"
#include "r_vss/r_motion_event_plugin_host.h"
#include "r_vss/r_stream_keeper.h"
#include "r_utils/r_file.h"
#include "r_disco/r_devices.h"
#include <memory>
#include <vector>
#include <algorithm>
#include <fstream>
#include <string>
#include <cmath>
#include <map>
#include <list>
#include <set>
#include <net.h>
#include <cpu.h>
#include <opencv2/opencv.hpp>

#ifdef IS_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace r_utils;

yolov8_person_plugin::yolov8_person_plugin(r_vss::r_motion_event_plugin_host* host)
    : _host(host),
      _initialized(false),
      _running(false)
{
    try {
        auto working_directory = r_fs::working_directory();

        auto params_path = working_directory + "/models/yolov8_person/yolov8n.param";
        auto bin_path = working_directory + "/models/yolov8_person/yolov8n.bin";

        // Create NCNN network instance
        _net = std::make_unique<ncnn::Net>();

        // Configure NCNN options BEFORE loading model
        _net->opt.use_vulkan_compute = false;  // Use CPU for now
        _net->opt.num_threads = ncnn::get_big_cpu_count();

        // Load model
        int ret_param = _net->load_param(params_path.c_str());
        if (ret_param != 0) {
            R_LOG_ERROR("yolov8_person_plugin: Failed to load model param file: %s (ret=%d)", params_path.c_str(), ret_param);
            _net.reset();
            return;
        }

        int ret_bin = _net->load_model(bin_path.c_str());
        if (ret_bin != 0) {
            R_LOG_ERROR("yolov8_person_plugin: Failed to load model bin file: %s (ret=%d)", bin_path.c_str(), ret_bin);
            _net.reset();
            return;
        }

        _initialized = true;

        // Start processing thread
        _running = true;
        _thread = std::thread(&yolov8_person_plugin::_entry_point, this);

    } catch (const std::exception& e) {
        R_LOG_ERROR("yolov8_person_plugin: Failed to initialize: %s", e.what());
        _initialized = false;
    }
}

yolov8_person_plugin::~yolov8_person_plugin()
{
    // Stop processing thread
    if (_running) {
        _running = false;

        // Wake up the thread by posting an empty message
        MotionEventMessage wake_msg;
        _event_queue.post(wake_msg);

        if (_thread.joinable()) {
            _thread.join();
        }
    }

    // Clean up network
    _net.reset();
}

void yolov8_person_plugin::post_motion_event(r_vss::r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height, const r_vss::motion_region& motion_bbox)
{
    if (!_running) {
        return;
    }

    MotionEventMessage msg;
    msg.evt = evt;
    msg.camera_id = camera_id;
    msg.ts = ts;
    msg.frame_data = frame_data;
    msg.width = width;
    msg.height = height;
    msg.motion_bbox = motion_bbox;

    R_LOG_INFO("yolov8_person_plugin: Motion event posted for camera %s", camera_id.c_str());

    _event_queue.post(msg);
}

void yolov8_person_plugin::_entry_point()
{
    while (_running) {
        try {
            auto maybe_msg = _event_queue.poll();

            if (!_running) {
                break;  // Exit if we're shutting down
            }

            if (!maybe_msg.is_null()) {
                _process_motion_event(maybe_msg.value());
            }

        } catch (const std::exception& e) {
            R_LOG_ERROR("yolov8_person_plugin: Error in worker thread: %s", e.what());
        }
    }
}

void yolov8_person_plugin::_process_motion_event(const MotionEventMessage& msg)
{
    if (!_initialized || !_host) {
        return;
    }

    try {
        if (msg.evt == r_vss::motion_event_start) {
            // Clear any existing detections for this camera (handle missing end events)
            _camera_detections[msg.camera_id].clear();
        }

        if (msg.evt == r_vss::motion_event_start || msg.evt == r_vss::motion_event_update) {
            // Process frame for start and update events
            size_t expected_size = msg.width * msg.height * 3;

            if (msg.frame_data.size() == expected_size) {
                // Run object detection and append to camera's detection list
                auto detections = detect_persons(msg.frame_data.data(), msg.width, msg.height, msg.camera_id, msg.ts, msg.motion_bbox);

                // Append all detections to the camera's list
                _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
            }
        }

        if (msg.evt == r_vss::motion_event_end) {
            // Analyze all detections for this motion sequence and log results
            _analyze_and_log_detections(msg.camera_id);

            // Clear the detection list for this camera
            _camera_detections[msg.camera_id].clear();
        }

    } catch (const std::exception& e) {
        R_LOG_ERROR("yolov8_person_plugin: Failed to process motion event: %s", e.what());
    }
}

std::vector<yolov8_person_plugin::Detection> yolov8_person_plugin::detect_persons(const uint8_t* rgb_data, int width, int height, const std::string& camera_id, int64_t timestamp, const r_vss::motion_region& motion_bbox)
{
    std::vector<Detection> detections;

    if (!_initialized || !_net) {
        return detections;
    }
    
    try {
        // YOLOv8 preprocessing: letterbox to 640x640 to maintain aspect ratio
        const int model_width = 640;
        const int model_height = 640;
        
        // Calculate scale factor to fit the image into model size while maintaining aspect ratio
        float scale = std::min((float)model_width / width, (float)model_height / height);
        int scaled_width = (int)(width * scale);
        int scaled_height = (int)(height * scale);
        
        // Calculate padding to center the image
        int pad_x = (model_width - scaled_width) / 2;
        int pad_y = (model_height - scaled_height) / 2;
        
        // Create letterboxed image with padding (input is now RGB)
        cv::Mat original_img(height, width, CV_8UC3, (void*)rgb_data);
        if (original_img.empty()) {
            return detections;
        }

        cv::Mat resized_img;
        cv::resize(original_img, resized_img, cv::Size(scaled_width, scaled_height));
        if (resized_img.empty()) {
            return detections;
        }

        // Create padded image (letterbox)
        cv::Mat letterbox_img = cv::Mat::zeros(model_height, model_width, CV_8UC3);
        cv::Rect roi(pad_x, pad_y, scaled_width, scaled_height);
        resized_img.copyTo(letterbox_img(roi));

        if (letterbox_img.empty()) {
            return detections;
        }

        // Convert to NCNN mat
        ncnn::Mat in = ncnn::Mat::from_pixels(letterbox_img.data, ncnn::Mat::PIXEL_BGR, model_width, model_height);
        if (in.empty()) {
            return detections;
        }
        
        // YOLOv8 normalization: divide by 255.0
        const float norm_vals[3] = {1.0f/255.0f, 1.0f/255.0f, 1.0f/255.0f};
        in.substract_mean_normalize(0, norm_vals);
        
        // Create extractor and run inference
        ncnn::Extractor ex = _net->create_extractor();
        
        // YOLOv8 input layer name
        int ret = ex.input("in0", in);
        if (ret != 0) {
            return detections;
        }

        // YOLOv8 output layer name
        ncnn::Mat out;
        ret = ex.extract("out0", out);
        if (ret != 0) {
            return detections;
        }
        
        // Check if output is empty
        if (out.empty()) {
            return detections;  // Empty detections is valid - nothing found
        }
        
        // YOLOv8 post-processing
        // Actual output format: w=8400, h=84, c=1 where 84 = 4 (bbox) + 80 (COCO classes)
        const float conf_threshold = 0.5f;  // Reasonable threshold for person detection
        const float nms_threshold = 0.45f;

        if (out.w == 8400 && out.c == 1) {
            int num_proposals = out.w;  // 8400 proposals
            int num_channels = out.h;   // 84 channels
            std::vector<Detection> proposals;

            for (int i = 0; i < num_proposals; i++) {
                // Access data for proposal i
                // Format: w=8400 (proposals), h=76 (channels)
                // Data is accessed as: out[channel_idx * 8400 + proposal_idx]

                // Get bbox coordinates (first 4 channels)
                const float* data = (const float*)out.data;
                float cx = data[0 * 8400 + i];  // Channel 0, proposal i
                float cy = data[1 * 8400 + i];  // Channel 1, proposal i
                float w = data[2 * 8400 + i];   // Channel 2, proposal i
                float h = data[3 * 8400 + i];   // Channel 3, proposal i

                // Find the class with maximum confidence (channels 4 to num_channels-1)
                float max_class_score = 0.0f;
                int max_class_id = -1;
                for (int j = 4; j < num_channels; j++) {
                    float class_score = data[j * 8400 + i];  // Channel j, proposal i
                    if (class_score > max_class_score) {
                        max_class_score = class_score;
                        max_class_id = j - 4;  // Subtract 4 to get 0-based class ID
                    }
                }

                if (max_class_score < conf_threshold) {
                    continue;
                }
                
                // Convert center format to corner format
                float x1 = cx - w * 0.5f;
                float y1 = cy - h * 0.5f;
                float x2 = cx + w * 0.5f;
                float y2 = cy + h * 0.5f;
                
                // Convert from letterbox space to original image space
                float orig_x1 = (x1 - pad_x) / scale;
                float orig_y1 = (y1 - pad_y) / scale;
                float orig_x2 = (x2 - pad_x) / scale;
                float orig_y2 = (y2 - pad_y) / scale;
                
                Detection detection;
                detection.x1 = orig_x1;
                detection.y1 = orig_y1;
                detection.x2 = orig_x2;
                detection.y2 = orig_y2;
                detection.score = max_class_score;
                detection.class_id = max_class_id;
                detection.camera_id = camera_id;
                detection.timestamp = timestamp;
                
                
                // Clamp to image bounds
                detection.x1 = std::max(0.0f, std::min((float)(width-1), detection.x1));
                detection.y1 = std::max(0.0f, std::min((float)(height-1), detection.y1));
                detection.x2 = std::max(0.0f, std::min((float)(width-1), detection.x2));
                detection.y2 = std::max(0.0f, std::min((float)(height-1), detection.y2));
                
                // Basic size validation - reject tiny or huge detections (temporarily disabled)
                // float bbox_width = detection.x2 - detection.x1;
                // float bbox_height = detection.y2 - detection.y1;
                // if (bbox_width < 10.0f || bbox_height < 10.0f || 
                //     bbox_width > width * 0.8f || bbox_height > height * 0.8f) {
                //     continue;
                // }
                
                // Since user only needs detection events (not bbox coordinates), add all detections above threshold
                proposals.push_back(detection);
            }
            
            // Apply NMS (Non-Maximum Suppression) - only within same class
            std::vector<int> indices;
            for (int i = 0; i < proposals.size(); i++) {
                indices.push_back(i);
            }
            
            // Sort by confidence (descending)
            std::sort(indices.begin(), indices.end(), [&](int a, int b) {
                return proposals[a].score > proposals[b].score;
            });
            
            std::vector<bool> suppressed(proposals.size(), false);
            
            for (int i = 0; i < indices.size(); i++) {
                if (suppressed[indices[i]]) continue;
                
                detections.push_back(proposals[indices[i]]);
                
                // Suppress overlapping detections of the same class
                for (int j = i + 1; j < indices.size(); j++) {
                    if (suppressed[indices[j]]) continue;
                    
                    const auto& det1 = proposals[indices[i]];
                    const auto& det2 = proposals[indices[j]];
                    
                    // Only suppress if same class
                    if (det1.class_id != det2.class_id) continue;
                    
                    // Calculate IoU
                    float inter_x1 = std::max(det1.x1, det2.x1);
                    float inter_y1 = std::max(det1.y1, det2.y1);
                    float inter_x2 = std::min(det1.x2, det2.x2);
                    float inter_y2 = std::min(det1.y2, det2.y2);
                    
                    if (inter_x1 < inter_x2 && inter_y1 < inter_y2) {
                        float inter_area = (inter_x2 - inter_x1) * (inter_y2 - inter_y1);
                        float area1 = (det1.x2 - det1.x1) * (det1.y2 - det1.y1);
                        float area2 = (det2.x2 - det2.x1) * (det2.y2 - det2.y1);
                        float union_area = area1 + area2 - inter_area;
                        
                        if (union_area > 0) {
                            float iou = inter_area / union_area;
                            if (iou > nms_threshold) {
                                suppressed[indices[j]] = true;
                            }
                        }
                    }
                }
            }

            // Filter detections based on motion region overlap
            if (motion_bbox.has_motion) {
                std::vector<Detection> filtered_detections;
                const float overlap_threshold = 0.1f; // 10% overlap required
                
                for (const auto& detection : detections) {
                    // Calculate intersection area between detection and motion region
                    float det_x1 = detection.x1;
                    float det_y1 = detection.y1;
                    float det_x2 = detection.x2;
                    float det_y2 = detection.y2;
                    
                    float motion_x1 = static_cast<float>(motion_bbox.x);
                    float motion_y1 = static_cast<float>(motion_bbox.y);
                    float motion_x2 = static_cast<float>(motion_bbox.x + motion_bbox.width);
                    float motion_y2 = static_cast<float>(motion_bbox.y + motion_bbox.height);
                    
                    // Calculate intersection
                    float inter_x1 = std::max(det_x1, motion_x1);
                    float inter_y1 = std::max(det_y1, motion_y1);
                    float inter_x2 = std::min(det_x2, motion_x2);
                    float inter_y2 = std::min(det_y2, motion_y2);
                    
                    if (inter_x1 < inter_x2 && inter_y1 < inter_y2) {
                        float inter_area = (inter_x2 - inter_x1) * (inter_y2 - inter_y1);
                        float det_area = (det_x2 - det_x1) * (det_y2 - det_y1);
                        
                        if (det_area > 0) {
                            float overlap_ratio = inter_area / det_area;
                            if (overlap_ratio >= overlap_threshold) {
                                filtered_detections.push_back(detection);
                            }
                        }
                    }
                }

                detections = filtered_detections;
            }
        }
        
    } catch (const std::exception& e) {
        R_LOG_ERROR("yolov8_person_plugin: YOLOv8 detection failed: %s", e.what());
    }
    
    return detections;
}

void yolov8_person_plugin::_analyze_and_log_detections(const std::string& camera_id)
{
    auto& devices = _host->get_devices();

    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null()) {
        return;
    }

    auto maybe_friendly_name = maybe_camera->friendly_name;

    auto it = _camera_detections.find(camera_id);
    if (it == _camera_detections.end() || it->second.empty()) {
        return;
    }

    // Count detections by class and track max confidence
    std::map<int, int> class_counts;
    std::map<int, float> max_confidence;

    for (const auto& detection : it->second) {
        class_counts[detection.class_id]++;
        if (max_confidence.find(detection.class_id) == max_confidence.end()) {
            max_confidence[detection.class_id] = detection.score;
        } else {
            max_confidence[detection.class_id] = std::max(max_confidence[detection.class_id], detection.score);
        }
    }

    // Log what we detected during the motion sequence with confidence
    for (const auto& pair : class_counts) {
        const char* class_name = get_class_name(pair.first);
        float confidence = max_confidence[pair.first];
        R_LOG_INFO("yolov8_person_plugin: camera: %s detected: %s (%d frames, max confidence: %.1f%%)",
                   (!maybe_friendly_name.is_null())?maybe_friendly_name.value().c_str():"unknown",
                   class_name, pair.second, confidence * 100);
    }

    // Get motion event timestamp from first detection
    int64_t motion_timestamp = it->second.front().timestamp;

    // Construct JSON metadata object
    std::string json_metadata = R"({"detections": [)";

    bool first = true;
    for (const auto& pair : class_counts) {
        const char* class_name = get_class_name(pair.first);
        float confidence = max_confidence[pair.first];

        if (!first) {
            json_metadata += ", ";
        }
        first = false;

        json_metadata += std::string(R"({"class_name": ")") + class_name +
                        R"(", "frame_count": )" + std::to_string(pair.second) +
                        R"(, "max_confidence": )" + std::to_string(confidence) + "}";
    }

    json_metadata += R"(], "total_detections": )" + std::to_string(it->second.size()) + "}";

    // Log metadata to stream
    auto& stream_keeper = _host->get_stream_keeper();
    stream_keeper.write_metadata(camera_id, "yolov8_person_plugin", json_metadata, motion_timestamp);
}

const char* yolov8_person_plugin::get_class_name(int class_id)
{
    // COCO dataset class names (80 classes)
    static const char* class_names[] = {
        "person", "bicycle", "car", "motorbike", "aeroplane", "bus", "train", "truck", "boat", "traffic light",
        "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
        "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
        "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
        "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "sofa",
        "pottedplant", "bed", "diningtable", "toilet", "tvmonitor", "laptop", "mouse", "remote", "keyboard",
        "cell phone", "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors",
        "teddy bear", "hair drier", "toothbrush"
    };
    
    const int num_classes = sizeof(class_names) / sizeof(class_names[0]);
    
    if (class_id >= 0 && class_id < num_classes) {
        return class_names[class_id];
    }
    return "unknown";
}

// Pure C API implementation - no C++ types in function signatures
extern "C"
{

R_API r_motion_plugin_handle load_plugin(r_motion_event_plugin_host_handle host)
{
    // Cast opaque handle back to actual type
    r_vss::r_motion_event_plugin_host* host_ptr = reinterpret_cast<r_vss::r_motion_event_plugin_host*>(host);

    // Create plugin instance
    yolov8_person_plugin* plugin = new yolov8_person_plugin(host_ptr);

    // Return as opaque handle
    return reinterpret_cast<r_motion_plugin_handle>(plugin);
}

R_API void destroy_plugin(r_motion_plugin_handle plugin)
{
    // Cast opaque handle back to actual type and delete
    yolov8_person_plugin* plugin_ptr = reinterpret_cast<yolov8_person_plugin*>(plugin);
    delete plugin_ptr;
}

R_API void post_motion_event(
    r_motion_plugin_handle plugin,
    int evt,
    const char* camera_id,
    int64_t ts,
    const uint8_t* frame_data,
    size_t frame_data_size,
    uint16_t width,
    uint16_t height,
    int motion_x,
    int motion_y,
    int motion_width,
    int motion_height,
    bool has_motion)
{
    // Cast opaque handle back to actual type
    yolov8_person_plugin* plugin_ptr = reinterpret_cast<yolov8_person_plugin*>(plugin);

    // Convert C types back to C++ types
    std::string camera_id_str(camera_id);
    std::vector<uint8_t> frame_data_vec(frame_data, frame_data + frame_data_size);
    r_vss::motion_region motion_bbox = {motion_x, motion_y, motion_width, motion_height, has_motion};
    r_vss::r_motion_event evt_enum = static_cast<r_vss::r_motion_event>(evt);

    // Call the C++ method
    plugin_ptr->post_motion_event(evt_enum, camera_id_str, ts, frame_data_vec, width, height, motion_bbox);
}

}