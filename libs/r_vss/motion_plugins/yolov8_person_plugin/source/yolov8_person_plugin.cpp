#include "yolov8_person_plugin.h"
#include "r_utils/r_logger.h"
#include "r_vss/r_motion_event_plugin_host.h"
#include "r_vss/r_stream_keeper.h"
#include "r_utils/r_file.h"
#include "r_disco/r_devices.h"
#include "r_utils/r_time_utils.h"
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
    stop();

    // Clean up network
    _net.reset();
}

void yolov8_person_plugin::stop()
{
    // Stop processing thread
    if (_running) {
        _running = false;

        FULL_MEM_BARRIER();

        _event_queue.wake();

        if (_thread.joinable()) {
            _thread.join();
        }
    }
}

void yolov8_person_plugin::post_motion_event(r_vss::r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height, const r_vss::motion_region& motion_bbox)
{
    if (!_running)
        return;

    MotionEventMessage msg;
    msg.evt = evt;
    msg.camera_id = camera_id;
    msg.ts = ts;
    msg.frame_data = frame_data;
    msg.width = width;
    msg.height = height;
    msg.motion_bbox = motion_bbox;

    if (evt == r_vss::motion_event_update) {
        // UPDATE frames: buffer them, and queue periodic frames every 5 seconds for long events
        const int64_t LONG_EVENT_INTERVAL_MS = 5000;
        bool should_queue_periodic = false;

        {
            std::lock_guard<std::mutex> lock(_buffer_mutex);
            _camera_buffered_update[camera_id] = msg; // Buffer the latest

            // Check if we should queue a periodic frame
            auto last_periodic_it = _camera_last_periodic_ts.find(camera_id);
            int64_t last_ts = (last_periodic_it != _camera_last_periodic_ts.end()) ? last_periodic_it->second : 0;

            if (last_ts > 0 && (ts - last_ts) >= LONG_EVENT_INTERVAL_MS) {
                should_queue_periodic = true;
                _camera_last_periodic_ts[camera_id] = ts;
            }
        }

        if (should_queue_periodic) {
            // Queue this frame as a periodic update
            const size_t MAX_QUEUE_DEPTH = 30;
            if (_event_queue.size() < MAX_QUEUE_DEPTH) {
                _event_queue.post(msg);
            }
        }
        return;
    }

    // START and END events go to the queue
    // Drop if queue is too deep (system performance limit)
    const size_t MAX_QUEUE_DEPTH = 30;
    if (_event_queue.size() >= MAX_QUEUE_DEPTH) {
        R_LOG_WARNING("yolov8_person_plugin: System performance limit exceeded - dropping frame (queue depth: %zu)", _event_queue.size());
        return;
    }

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
            _camera_disproven_classes[msg.camera_id].clear();
            {
                std::lock_guard<std::mutex> lock(_buffer_mutex);
                _camera_buffered_update.erase(msg.camera_id);
                _camera_last_periodic_ts[msg.camera_id] = msg.ts; // Initialize for periodic frame tracking
            }
            // Record motion start time
            _camera_motion_start_time[msg.camera_id] = msg.ts;

            // Process START frame immediately
            size_t expected_size = msg.width * msg.height * 3;
            if (msg.frame_data.size() == expected_size) {
                auto detections = detect_persons(msg.frame_data.data(), msg.width, msg.height, msg.camera_id, msg.ts, msg.motion_bbox);
                R_LOG_INFO("YOLOV8_DIAG: START frame - detect_persons returned %zu detections", detections.size());
                _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
                _camera_last_processed_ts[msg.camera_id] = msg.ts;
            }
        }

        // Periodic UPDATE events come through the queue for long events
        if (msg.evt == r_vss::motion_event_update) {
            size_t expected_size = msg.width * msg.height * 3;
            if (msg.frame_data.size() == expected_size) {
                auto detections = detect_persons(msg.frame_data.data(), msg.width, msg.height, msg.camera_id, msg.ts, msg.motion_bbox);
                R_LOG_INFO("YOLOV8_DIAG: PERIODIC frame - detect_persons returned %zu detections", detections.size());
                _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
                _camera_last_processed_ts[msg.camera_id] = msg.ts;
            }
        }

        if (msg.evt == r_vss::motion_event_end) {
            // Grab the buffered UPDATE frame (if any) under lock, and clear periodic tracking
            MotionEventMessage buffered;
            bool have_buffered = false;
            {
                std::lock_guard<std::mutex> lock(_buffer_mutex);
                auto buffered_it = _camera_buffered_update.find(msg.camera_id);
                if (buffered_it != _camera_buffered_update.end()) {
                    buffered = std::move(buffered_it->second);
                    have_buffered = true;
                    _camera_buffered_update.erase(buffered_it);
                }
                _camera_last_periodic_ts.erase(msg.camera_id);
            }

            // Process buffered UPDATE frame if we have one (middle frame)
            // Skip if it's older than the last processed frame (we already sent a PERIODIC frame after it)
            if (have_buffered) {
                auto last_ts_it = _camera_last_processed_ts.find(msg.camera_id);
                bool should_process = (last_ts_it == _camera_last_processed_ts.end()) ||
                                      (buffered.ts > last_ts_it->second);

                if (should_process) {
                    size_t expected_size = buffered.width * buffered.height * 3;
                    if (buffered.frame_data.size() == expected_size) {
                        auto detections = detect_persons(buffered.frame_data.data(), buffered.width, buffered.height, buffered.camera_id, buffered.ts, buffered.motion_bbox);
                        R_LOG_INFO("YOLOV8_DIAG: MIDDLE frame - detect_persons returned %zu detections", detections.size());
                        _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
                    }
                }
            }

            // Process END frame
            size_t expected_size = msg.width * msg.height * 3;
            if (msg.frame_data.size() == expected_size) {
                auto detections = detect_persons(msg.frame_data.data(), msg.width, msg.height, msg.camera_id, msg.ts, msg.motion_bbox);
                R_LOG_INFO("YOLOV8_DIAG: END frame - detect_persons returned %zu detections", detections.size());
                _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
            }

            R_LOG_INFO("YOLOV8_DIAG: EVENT_END - total detections accumulated: %zu",
                       _camera_detections[msg.camera_id].size());

            // Analyze all detections for this motion sequence and log results
            _analyze_and_log_detections(msg.camera_id, msg.ts);

            // Clear the detection list for this camera
            _camera_detections[msg.camera_id].clear();
            _camera_motion_start_time.erase(msg.camera_id);
            _camera_disproven_classes.erase(msg.camera_id);
            _camera_last_processed_ts.erase(msg.camera_id);
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
        const float conf_threshold = 0.65f;  // Higher threshold to reduce false positives (e.g., car misclassified as train)
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
            std::vector<Detection> nms_detections;

            for (int i = 0; i < indices.size(); i++) {
                if (suppressed[indices[i]]) continue;

                nms_detections.push_back(proposals[indices[i]]);

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

            // Filter detections that don't overlap sufficiently with the motion region
            // Require at least 25% of the detection bbox to overlap with the motion bbox
            const float min_overlap_ratio = 0.25f;

            if (motion_bbox.has_motion && motion_bbox.width > 0 && motion_bbox.height > 0) {
                // Motion bbox coordinates (in original image space at detection resolution)
                float motion_x1 = (float)motion_bbox.x;
                float motion_y1 = (float)motion_bbox.y;
                float motion_x2 = (float)(motion_bbox.x + motion_bbox.width);
                float motion_y2 = (float)(motion_bbox.y + motion_bbox.height);

                for (const auto& det : nms_detections) {
                    // Calculate intersection with motion region
                    float inter_x1 = std::max(det.x1, motion_x1);
                    float inter_y1 = std::max(det.y1, motion_y1);
                    float inter_x2 = std::min(det.x2, motion_x2);
                    float inter_y2 = std::min(det.y2, motion_y2);

                    float det_area = (det.x2 - det.x1) * (det.y2 - det.y1);

                    if (inter_x1 < inter_x2 && inter_y1 < inter_y2 && det_area > 0) {
                        float inter_area = (inter_x2 - inter_x1) * (inter_y2 - inter_y1);
                        float overlap_ratio = inter_area / det_area;

                        if (overlap_ratio >= min_overlap_ratio) {
                            detections.push_back(det);
                        } else {
                            R_LOG_INFO("yolov8_person_plugin: Filtered detection (class %d, conf %.2f) - only %.1f%% overlap with motion region",
                                       det.class_id, det.score, overlap_ratio * 100.0f);
                        }
                    } else {
                        R_LOG_INFO("yolov8_person_plugin: Filtered detection (class %d, conf %.2f) - no overlap with motion region",
                                   det.class_id, det.score);
                    }
                }
            } else {
                // No valid motion region, keep all detections
                detections = std::move(nms_detections);
            }
        }
        
    } catch (const std::exception& e) {
        R_LOG_ERROR("yolov8_person_plugin: YOLOv8 detection failed: %s", e.what());
    }
    
    return detections;
}

void yolov8_person_plugin::_analyze_and_log_detections(const std::string& camera_id, int64_t end_time_ms)
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

    // Get start time for this motion event
    int64_t start_time_ms = end_time_ms; // fallback if not found
    auto start_it = _camera_motion_start_time.find(camera_id);
    if (start_it != _camera_motion_start_time.end()) {
        start_time_ms = start_it->second;
    }

    // Get disproven classes for this camera
    std::set<int> disproven;
    auto disproven_it = _camera_disproven_classes.find(camera_id);
    if (disproven_it != _camera_disproven_classes.end()) {
        disproven = disproven_it->second;
    }

    // Count detections by class for logging (excluding disproven classes)
    std::map<int, int> class_counts;
    std::map<int, float> max_confidence;

    for (const auto& detection : it->second) {
        // Skip disproven classes - they were detected but not consistently overlapping motion
        if (disproven.find(detection.class_id) != disproven.end()) {
            continue;
        }
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

    // Log disproven classes for debugging
    for (int class_id : disproven) {
        const char* class_name = get_class_name(class_id);
        R_LOG_INFO("yolov8_person_plugin: camera: %s disproven class: %s (detected but not consistently in motion)",
                   (!maybe_friendly_name.is_null())?maybe_friendly_name.value().c_str():"unknown",
                   class_name);
    }

    // Convert timestamps to ISO 8601 format (UTC)
    auto start_tp = r_time_utils::epoch_millis_to_tp(start_time_ms);
    auto end_tp = r_time_utils::epoch_millis_to_tp(end_time_ms);
    std::string start_time_str = r_time_utils::tp_to_iso_8601(start_tp, true);
    std::string end_time_str = r_time_utils::tp_to_iso_8601(end_tp, true);

    // Construct JSON metadata object with analytics schema
    std::string json_metadata = R"({"analytics": {"motion_start_time": ")" + start_time_str +
                                R"(", "motion_end_time": ")" + end_time_str +
                                R"(", "detections": [)";

    bool first = true;
    int valid_detection_count = 0;
    for (const auto& detection : it->second) {
        // Skip disproven classes in JSON output
        if (disproven.find(detection.class_id) != disproven.end()) {
            continue;
        }

        if (!first) {
            json_metadata += ", ";
        }
        first = false;
        valid_detection_count++;

        auto det_tp = r_time_utils::epoch_millis_to_tp(detection.timestamp);
        std::string det_time_str = r_time_utils::tp_to_iso_8601(det_tp, true);
        const char* class_name = get_class_name(detection.class_id);

        // Format confidence to 3 decimal places
        char conf_buf[32];
        snprintf(conf_buf, sizeof(conf_buf), "%.3f", detection.score);

        json_metadata += R"({"timestamp": ")" + det_time_str +
                        R"(", "class_name": ")" + class_name +
                        R"(", "confidence": )" + conf_buf + "}";
    }

    json_metadata += R"(], "total_detections": )" + std::to_string(valid_detection_count) + "}}";

    // Only write metadata if we have valid detections
    if (valid_detection_count > 0) {
        auto& stream_keeper = _host->get_stream_keeper();
        stream_keeper.write_metadata(camera_id, "yolov8_person_plugin", json_metadata, start_time_ms);
    }
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

R_API void stop_plugin(r_motion_plugin_handle plugin)
{
    // Cast opaque handle back to actual type and stop
    yolov8_person_plugin* plugin_ptr = reinterpret_cast<yolov8_person_plugin*>(plugin);
    plugin_ptr->stop();
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