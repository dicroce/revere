#include "picodet_person_plugin.h"
#include "r_utils/r_logger.h"
#include "r_vss/r_query.h"
#include "r_vss/r_motion_event_plugin_host.h"
#include "r_vss/r_stream_keeper.h"
#include "r_utils/r_time_utils.h"
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

picodet_person_plugin::picodet_person_plugin(r_vss::r_motion_event_plugin_host* host)
    : _host(host),
      _initialized(false),
      _running(false)
{
    R_LOG_INFO("picodet_person_plugin: Constructor called");
    
    try {
        auto working_directory = r_fs::working_directory();

        auto params_path = working_directory + "/models/picodet_person/picodet_s_416.param";
        auto bin_path = working_directory + "/models/picodet_person/picodet_s_416.bin";

        R_LOG_INFO("picodet_person_plugin: Loading PicoDet model from %s", params_path.c_str());

        // Create NCNN network instance
        _net = std::make_unique<ncnn::Net>();
        
        // Configure NCNN options BEFORE loading model
        _net->opt.use_vulkan_compute = false;  // Use CPU for now
        _net->opt.num_threads = ncnn::get_big_cpu_count();
        
        // Load model
        int ret_param = _net->load_param(params_path.c_str());
        if (ret_param != 0) {
            R_LOG_ERROR("picodet_person_plugin: Failed to load model param file: %s (ret=%d)", params_path.c_str(), ret_param);
            _net.reset();
            return;
        }
        
        int ret_bin = _net->load_model(bin_path.c_str());
        if (ret_bin != 0) {
            R_LOG_ERROR("picodet_person_plugin: Failed to load model bin file: %s (ret=%d)", bin_path.c_str(), ret_bin);
            _net.reset();
            return;
        }
        
        _initialized = true;
        R_LOG_INFO("picodet_person_plugin: PicoDet model loaded successfully");
        
        // Start processing thread
        _running = true;
        _thread = std::thread(&picodet_person_plugin::_entry_point, this);
        
    } catch (const std::exception& e) {
        R_LOG_ERROR("picodet_person_plugin: Failed to initialize: %s", e.what());
        _initialized = false;
    }
}

picodet_person_plugin::~picodet_person_plugin()
{
    R_LOG_INFO("picodet_person_plugin: Destructor called");
    
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
    
    R_LOG_INFO("picodet_person_plugin: Destructor completed");
}

void picodet_person_plugin::post_motion_event(r_vss::r_motion_event evt, const std::string& camera_id, int64_t ts)
{
    if (!_running) {
        return;
    }
    
    MotionEventMessage msg;
    msg.evt = evt;
    msg.camera_id = camera_id;
    msg.ts = ts;
    
    _event_queue.post(msg);
}

void picodet_person_plugin::_entry_point()
{
    R_LOG_INFO("picodet_person_plugin: Worker thread started processing events");
    
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
            R_LOG_ERROR("picodet_person_plugin: Error in worker thread: %s", e.what());
        }
    }
    
    R_LOG_INFO("picodet_person_plugin: Worker thread exiting");
}

void picodet_person_plugin::_process_motion_event(const MotionEventMessage& msg)
{
    if (!_initialized || !_host) {
        R_LOG_WARNING("picodet_person_plugin: Plugin not initialized, skipping event");
        return;
    }
    
    try {
        if (msg.evt == r_vss::motion_event_start) {
            // Clear any existing detections for this camera (handle missing end events)
            _camera_detections[msg.camera_id].clear();
        }
        
        if (msg.evt == r_vss::motion_event_start || msg.evt == r_vss::motion_event_update) {
            // Process frame for start and update events
            auto time_point = r_utils::r_time_utils::epoch_millis_to_tp(msg.ts);
            
            const uint16_t original_width = 640;
            const uint16_t original_height = 480;
            
            auto bgr24_frame = r_vss::query_get_bgr24_frame(
                _host->get_top_dir(),
                _host->get_devices(),
                msg.camera_id,
                time_point,
                original_width,
                original_height
            );
            
            size_t expected_size = original_width * original_height * 3;
            
            if (bgr24_frame.size() == expected_size) {
                // Run object detection and append to camera's detection list
                auto detections = detect_persons(bgr24_frame.data(), original_width, original_height, msg.camera_id, msg.ts);
                
                // Append all detections to the camera's list
                _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
            } else {
                R_LOG_ERROR("picodet_person_plugin: Unexpected frame size for camera %s", msg.camera_id.c_str());
            }
        }
        
        if (msg.evt == r_vss::motion_event_end) {
            // Analyze all detections for this motion sequence and log results
            _analyze_and_log_detections(msg.camera_id);
            
            // Clear the detection list for this camera
            _camera_detections[msg.camera_id].clear();
        }
        
    } catch (const std::exception& e) {
        R_LOG_ERROR("picodet_person_plugin: Failed to process motion event: %s", e.what());
    }
}

std::vector<picodet_person_plugin::Detection> picodet_person_plugin::detect_persons(const uint8_t* bgr_data, int width, int height, const std::string& camera_id, int64_t timestamp)
{
    std::vector<Detection> detections;
    
    if (!_initialized || !_net) {
        R_LOG_ERROR("picodet_person_plugin: Network not initialized");
        return detections;
    }
    
    try {
        // PicoDet preprocessing: letterbox to 416x416 to maintain aspect ratio
        const int model_width = 416;
        const int model_height = 416;
        
        // Calculate scale factor to fit the image into model size while maintaining aspect ratio
        float scale = std::min((float)model_width / width, (float)model_height / height);
        int scaled_width = (int)(width * scale);
        int scaled_height = (int)(height * scale);
        
        // Calculate padding to center the image
        int pad_x = (model_width - scaled_width) / 2;
        int pad_y = (model_height - scaled_height) / 2;
        
        // Create letterboxed image with padding
        cv::Mat original_img(height, width, CV_8UC3, (void*)bgr_data);
        if (original_img.empty()) {
            R_LOG_ERROR("picodet_person_plugin: Failed to create OpenCV Mat from BGR data");
            return detections;
        }
        
        cv::Mat resized_img;
        cv::resize(original_img, resized_img, cv::Size(scaled_width, scaled_height));
        if (resized_img.empty()) {
            R_LOG_ERROR("picodet_person_plugin: Failed to resize image");
            return detections;
        }
        
        // Create padded image (letterbox)
        cv::Mat letterbox_img = cv::Mat::zeros(model_height, model_width, CV_8UC3);
        cv::Rect roi(pad_x, pad_y, scaled_width, scaled_height);
        resized_img.copyTo(letterbox_img(roi));
        
        if (letterbox_img.empty()) {
            R_LOG_ERROR("picodet_person_plugin: Failed to create letterbox image");
            return detections;
        }
        
        // Convert to NCNN mat
        ncnn::Mat in = ncnn::Mat::from_pixels(letterbox_img.data, ncnn::Mat::PIXEL_BGR, model_width, model_height);
        if (in.empty()) {
            R_LOG_ERROR("picodet_person_plugin: Failed to create NCNN Mat from letterbox image");
            return detections;
        }
        
        // PicoDet normalization: ImageNet normalization
        // mean=[123.675, 116.28, 103.53], std=[58.395, 57.12, 57.375]
        const float mean_vals[3] = {123.675f, 116.28f, 103.53f};
        const float norm_vals[3] = {1.0f/58.395f, 1.0f/57.12f, 1.0f/57.375f};
        in.substract_mean_normalize(mean_vals, norm_vals);
        
        // Create extractor and run inference
        ncnn::Extractor ex = _net->create_extractor();
        
        // PicoDet input layer name (from param file analysis)
        int ret = ex.input("image", in);
        if (ret != 0) {
            R_LOG_ERROR("picodet_person_plugin: Failed to set input data (ret=%d)", ret);
            return detections;
        }
        
        // PicoDet has multiple outputs for different scales
        // We'll use the first scale output: save_infer_model/scale_0.tmp_1 (class scores)
        // and save_infer_model/scale_4.tmp_1 (bbox coordinates)
        ncnn::Mat cls_out, bbox_out;
        
        ret = ex.extract("save_infer_model/scale_0.tmp_1", cls_out);
        if (ret != 0) {
            R_LOG_ERROR("picodet_person_plugin: Failed to extract class output (ret=%d)", ret);
            return detections;
        }
        
        ret = ex.extract("save_infer_model/scale_4.tmp_1", bbox_out);
        if (ret != 0) {
            R_LOG_ERROR("picodet_person_plugin: Failed to extract bbox output (ret=%d)", ret);
            return detections;
        }
        
        // Check if outputs are empty
        if (cls_out.empty() || bbox_out.empty()) {
            return detections;  // Empty detections is valid - nothing found
        }
        
        // PicoDet post-processing
        // cls_out contains class probabilities, bbox_out contains box coordinates
        const float conf_threshold = 0.3f;  // Lower threshold for debugging
        
        R_LOG_DEBUG("picodet_person_plugin: cls_out: w=%d h=%d c=%d, bbox_out: w=%d h=%d c=%d", 
                   cls_out.w, cls_out.h, cls_out.c, bbox_out.w, bbox_out.h, bbox_out.c);
        
        // PicoDet outputs: cls_out [num_anchors, 80] and bbox_out [num_anchors, 32]
        // The 32 in bbox_out represents 4 coordinates × 8 distribution bins each
        if (cls_out.h == bbox_out.h && cls_out.w == 80 && bbox_out.w == 32) {
            int num_anchors = cls_out.h;
            int num_classes = cls_out.w;
            
            // Debug: Check max scores across all anchors
            float global_max_score = 0.0f;
            int global_max_class = -1;
            int global_max_anchor = -1;
            
            // Debug: Check first few iterations
            static int debug_iterations = 0;
            
            for (int i = 0; i < num_anchors; i++) {
                const float* cls_scores = cls_out.row(i);
                const float* bbox_dist = bbox_out.row(i);
                
                // Find the class with maximum score
                float max_score = 0.0f;
                int max_class_id = -1;
                
                // Debug first few anchors
                if (debug_iterations < 5 && i < 10) {
                    R_LOG_INFO("picodet_person_plugin: Anchor %d starting processing", i);
                    debug_iterations++;
                }
                
                for (int j = 0; j < num_classes; j++) {
                    // Try without sigmoid first - PicoDet outputs might be pre-activated
                    float score = cls_scores[j];
                    
                    if (score > max_score) {
                        max_score = score;
                        max_class_id = j;
                    }
                    
                    // Track global maximum
                    if (score > global_max_score) {
                        global_max_score = score;
                        global_max_class = j;
                        global_max_anchor = i;
                    }
                }
                
                // Debug threshold check for first few
                if (debug_iterations <= 5 && i < 10) {
                    R_LOG_INFO("picodet_person_plugin: Anchor %d - max_score: %.3f, threshold: %.3f, class: %s",
                              i, max_score, conf_threshold, get_class_name(max_class_id));
                }
                
                // Skip if confidence is too low
                if (max_score < conf_threshold) {
                    continue;
                }
                
                // Debug: we passed the threshold (just first few to avoid spam)
                static int passed_threshold_count = 0;
                if (passed_threshold_count < 3) {
                    R_LOG_INFO("picodet_person_plugin: Detection passed threshold - class: %s, score: %.3f, anchor: %d",
                              get_class_name(max_class_id), max_score, i);
                    
                    // Also log the raw bbox distribution values for debugging
                    const float* bbox_dist = bbox_out.row(i);
                    R_LOG_INFO("picodet_person_plugin: First 8 bbox values: [%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]",
                              bbox_dist[0], bbox_dist[1], bbox_dist[2], bbox_dist[3], 
                              bbox_dist[4], bbox_dist[5], bbox_dist[6], bbox_dist[7]);
                    passed_threshold_count++;
                }
                
                // PicoDet bbox decoding: values seem to be in a different scale
                // Let's try normalizing by a factor to get reasonable coordinates
                float raw_x1 = bbox_dist[0];
                float raw_y1 = bbox_dist[1]; 
                float raw_x2 = bbox_dist[2];
                float raw_y2 = bbox_dist[3];
                
                // Debug first few bbox decodings
                static int bbox_debug_count = 0;
                if (bbox_debug_count < 5) {
                    R_LOG_INFO("picodet_person_plugin: Raw bbox coords: [%.3f, %.3f, %.3f, %.3f]",
                              raw_x1, raw_y1, raw_x2, raw_y2);
                    bbox_debug_count++;
                }
                
                // Try normalizing by dividing by model dimensions to get 0-1 range
                float norm_x1 = raw_x1 / model_width;
                float norm_y1 = raw_y1 / model_height;
                float norm_x2 = raw_x2 / model_width;
                float norm_y2 = raw_y2 / model_height;
                
                // Clamp to 0-1 range
                norm_x1 = std::max(0.0f, std::min(1.0f, norm_x1));
                norm_y1 = std::max(0.0f, std::min(1.0f, norm_y1));
                norm_x2 = std::max(0.0f, std::min(1.0f, norm_x2));
                norm_y2 = std::max(0.0f, std::min(1.0f, norm_y2));
                
                // Convert normalized coordinates to letterbox pixel coordinates
                float letterbox_x1 = norm_x1 * model_width;
                float letterbox_y1 = norm_y1 * model_height;
                float letterbox_x2 = norm_x2 * model_width;
                float letterbox_y2 = norm_y2 * model_height;
                
                // Convert from letterbox space to original image space
                float x1 = (letterbox_x1 - pad_x) / scale;
                float y1 = (letterbox_y1 - pad_y) / scale;
                float x2 = (letterbox_x2 - pad_x) / scale;
                float y2 = (letterbox_y2 - pad_y) / scale;
                
                Detection detection;
                detection.x1 = x1;
                detection.y1 = y1;
                detection.x2 = x2;
                detection.y2 = y2;
                detection.score = max_score;
                detection.class_id = max_class_id;
                detection.camera_id = camera_id;
                detection.timestamp = timestamp;
                
                // Clamp to image bounds
                detection.x1 = std::max(0.0f, std::min((float)(width-1), detection.x1));
                detection.y1 = std::max(0.0f, std::min((float)(height-1), detection.y1));
                detection.x2 = std::max(0.0f, std::min((float)(width-1), detection.x2));
                detection.y2 = std::max(0.0f, std::min((float)(height-1), detection.y2));
                
                // Filter out tiny bounding boxes
                float box_width = detection.x2 - detection.x1;
                float box_height = detection.y2 - detection.y1;
                const float min_box_size = 10.0f;
                
                if (box_width > min_box_size && box_height > min_box_size) {
                    detections.push_back(detection);
                    
                    // Log first few detections for debugging
                    if (detections.size() <= 3) {
                        R_LOG_INFO("picodet_person_plugin: Detection %d - class: %s, score: %.3f, bbox: [%.1f,%.1f,%.1f,%.1f]",
                                  (int)detections.size(), get_class_name(max_class_id), max_score, x1, y1, x2, y2);
                    }
                }
            }
            
            // Debug: Log the highest score found
            R_LOG_INFO("picodet_person_plugin: Highest score found: %.4f for class %s at anchor %d",
                      global_max_score, get_class_name(global_max_class), global_max_anchor);
                      
            // Also check for person class specifically
            float max_person_score = 0.0f;
            int person_anchor = -1;
            for (int i = 0; i < num_anchors; i++) {
                const float* cls_scores = cls_out.row(i);
                float person_score = 1.0f / (1.0f + exp(-cls_scores[0])); // person is class 0 in COCO
                if (person_score > max_person_score) {
                    max_person_score = person_score;
                    person_anchor = i;
                }
            }
            R_LOG_INFO("picodet_person_plugin: Max person score: %.4f at anchor %d", max_person_score, person_anchor);
            
            // Debug: Log how many detections pass threshold
            int detections_above_threshold = 0;
            for (int i = 0; i < num_anchors; i++) {
                const float* cls_scores = cls_out.row(i);
                float max_anchor_score = 0.0f;
                for (int j = 0; j < num_classes; j++) {
                    float score = 1.0f / (1.0f + exp(-cls_scores[j]));
                    if (score > max_anchor_score) {
                        max_anchor_score = score;
                    }
                }
                if (max_anchor_score >= conf_threshold) {
                    detections_above_threshold++;
                }
            }
            R_LOG_INFO("picodet_person_plugin: %d anchors have scores >= %.2f threshold", detections_above_threshold, conf_threshold);
                      
            // If no detections were added but we had high scores, log why
            if (detections.empty() && global_max_score > 0.1f) {
                R_LOG_INFO("picodet_person_plugin: No detections added despite score %.3f - checking bbox decoding",
                          global_max_score);
            }
        } else {
            R_LOG_DEBUG("picodet_person_plugin: Unexpected output format - cls: w=%d h=%d c=%d, bbox: w=%d h=%d c=%d", 
                       cls_out.w, cls_out.h, cls_out.c, bbox_out.w, bbox_out.h, bbox_out.c);
        }
        
    } catch (const std::exception& e) {
        R_LOG_ERROR("picodet_person_plugin: PicoDet detection failed: %s", e.what());
    }
    
    return detections;
}

void picodet_person_plugin::_analyze_and_log_detections(const std::string& camera_id)
{
    auto& devices = _host->get_devices();

    auto maybe_camera = devices.get_camera_by_id(camera_id);

    if(maybe_camera.is_null()) {
        R_LOG_ERROR("Unable to fetch camera for analasys.");
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
        R_LOG_INFO("picodet_person_plugin: camera: %s detected: %s (%d frames, max confidence: %.1f%%)", 
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
    stream_keeper.write_metadata(camera_id, "picodet_person_plugin", json_metadata, motion_timestamp);
    
    R_LOG_DEBUG("picodet_person_plugin: Analyzed %d total detections for camera %s", 
                (int)it->second.size(), camera_id.c_str());
}

const char* picodet_person_plugin::get_class_name(int class_id)
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
    R_LOG_INFO("picodet_person_plugin: load_plugin() called");

    // Cast opaque handle back to actual type
    r_vss::r_motion_event_plugin_host* host_ptr = reinterpret_cast<r_vss::r_motion_event_plugin_host*>(host);

    // Create plugin instance
    picodet_person_plugin* plugin = new picodet_person_plugin(host_ptr);

    // Return as opaque handle
    return reinterpret_cast<r_motion_plugin_handle>(plugin);
}

R_API void destroy_plugin(r_motion_plugin_handle plugin)
{
    // Cast opaque handle back to actual type and delete
    picodet_person_plugin* plugin_ptr = reinterpret_cast<picodet_person_plugin*>(plugin);
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
    picodet_person_plugin* plugin_ptr = reinterpret_cast<picodet_person_plugin*>(plugin);

    // Convert C types back to C++ types
    std::string camera_id_str(camera_id);
    std::vector<uint8_t> frame_data_vec(frame_data, frame_data + frame_data_size);
    r_vss::motion_region motion_bbox = {motion_x, motion_y, motion_width, motion_height, has_motion};
    r_vss::r_motion_event evt_enum = static_cast<r_vss::r_motion_event>(evt);

    // Call the C++ method
    plugin_ptr->post_motion_event(evt_enum, camera_id_str, ts, frame_data_vec, width, height, motion_bbox);
}

}