#include "yolov8_motion_plugin.h"
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

yolov8_motion_plugin::yolov8_motion_plugin(r_vss::r_motion_event_plugin_host* host)
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
            R_LOG_ERROR("yolov8_motion_plugin: Failed to load model param file: %s (ret=%d)", params_path.c_str(), ret_param);
            _net.reset();
            return;
        }

        int ret_bin = _net->load_model(bin_path.c_str());
        if (ret_bin != 0) {
            R_LOG_ERROR("yolov8_motion_plugin: Failed to load model bin file: %s (ret=%d)", bin_path.c_str(), ret_bin);
            _net.reset();
            return;
        }

        _initialized = true;

        // Static anchors learnt before a restart (see r_vss::r_early_alerts).
        auto anchors_path = _anchors_path();
        if (!anchors_path.empty() && r_fs::file_exists(anchors_path)) {
            auto bytes = r_fs::read_file(anchors_path);
            _early.from_json(std::string(bytes.begin(), bytes.end()));
        }

        // Start processing thread
        _running = true;
        _thread = std::thread(&yolov8_motion_plugin::_entry_point, this);

    } catch (const std::exception& e) {
        R_LOG_ERROR("yolov8_motion_plugin: Failed to initialize: %s", e.what());
        _initialized = false;
    }
}

yolov8_motion_plugin::~yolov8_motion_plugin()
{
    stop();

    // Clean up network
    _net.reset();
}

void yolov8_motion_plugin::stop()
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

void yolov8_motion_plugin::post_motion_event(r_vss::r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height, const r_vss::motion_region& motion_bbox)
{
    post_motion_event(evt, camera_id, ts, frame_data, width, height, motion_bbox, {});
}

void yolov8_motion_plugin::post_motion_event(r_vss::r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height, const r_vss::motion_region& motion_bbox, const std::vector<r_vss::motion_region>& motion_regions)
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
    msg.motion_regions = motion_regions;

    if (evt == r_vss::motion_event_update) {
        // UPDATE frames: buffer them, and queue periodic frames: every second
        // until the event has sent an early alert, then every 5 seconds.
        const int64_t LONG_EVENT_INTERVAL_MS = _early.look_interval_ms(camera_id);
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
        R_LOG_WARNING("yolov8_motion_plugin: System performance limit exceeded - dropping frame (queue depth: %zu)", _event_queue.size());
        return;
    }

    _event_queue.post(msg);
}

void yolov8_motion_plugin::_entry_point()
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
            R_LOG_ERROR("yolov8_motion_plugin: Error in worker thread: %s", e.what());
        }
    }
}

void yolov8_motion_plugin::_process_motion_event(const MotionEventMessage& msg)
{
    if (!_initialized || !_host) {
        return;
    }

    try {
        if (msg.evt == r_vss::motion_event_start) {
            // Clear any existing detections for this camera (handle missing end events)
            _camera_detections[msg.camera_id].clear();
            _camera_disproven_classes[msg.camera_id].clear();
            _camera_start_frame_detections.erase(msg.camera_id);
            _camera_end_frame_detections.erase(msg.camera_id);
            {
                std::lock_guard<std::mutex> lock(_buffer_mutex);
                _camera_buffered_update.erase(msg.camera_id);
                _camera_last_periodic_ts[msg.camera_id] = msg.ts; // Initialize for periodic frame tracking
            }
            // Record motion start time
            _camera_motion_start_time[msg.camera_id] = msg.ts;
            _early.event_started(msg.camera_id);

            // Process START frame immediately
            size_t expected_size = msg.width * msg.height * 3;
            if (msg.frame_data.size() == expected_size) {
                std::vector<Detection> endpoint_detections;
                auto detections = detect_persons(msg.frame_data.data(), msg.width, msg.height, msg.camera_id, msg.ts, msg.motion_bbox, msg.motion_regions, &endpoint_detections);
                _camera_start_frame_detections[msg.camera_id] = std::move(endpoint_detections);
                if(r_vss::has_valid_motion_evidence(msg.motion_bbox, msg.motion_regions))
                {
                    _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
                    _early_alerts_for(msg.camera_id, detections);
                }
                _camera_last_processed_ts[msg.camera_id] = msg.ts;
            }
        }

        // Periodic UPDATE events come through the queue for long events
        if (msg.evt == r_vss::motion_event_update) {
            size_t expected_size = msg.width * msg.height * 3;
            if (msg.frame_data.size() == expected_size) {
                auto detections = detect_persons(msg.frame_data.data(), msg.width, msg.height, msg.camera_id, msg.ts, msg.motion_bbox, msg.motion_regions);
                _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
                _camera_last_processed_ts[msg.camera_id] = msg.ts;
                _early_alerts_for(msg.camera_id, detections);
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
                        auto detections = detect_persons(buffered.frame_data.data(), buffered.width, buffered.height, buffered.camera_id, buffered.ts, buffered.motion_bbox, buffered.motion_regions);
                        _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
                    }
                }
            }

            // Process END frame
            size_t expected_size = msg.width * msg.height * 3;
            if (msg.frame_data.size() == expected_size) {
                std::vector<Detection> endpoint_detections;
                auto detections = detect_persons(msg.frame_data.data(), msg.width, msg.height, msg.camera_id, msg.ts, msg.motion_bbox, msg.motion_regions, &endpoint_detections);
                _camera_end_frame_detections[msg.camera_id] = std::move(endpoint_detections);
                // Endpoint inference is also used to identify static objects. Do not
                // promote an unfiltered END-frame recognition when the frame itself
                // contains no current motion evidence.
                if(r_vss::has_valid_motion_evidence(msg.motion_bbox, msg.motion_regions))
                    _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
            }

            // Analyze all detections for this motion sequence and log results
            _analyze_and_log_detections(msg.camera_id, msg.ts);

            // Learn static anchors from the frames that started and ended it.
            std::vector<r_vss::r_early_alerts::r_seen> start_seen, end_seen;
            for (const auto& d : _camera_start_frame_detections[msg.camera_id])
                start_seen.push_back({get_class_name(d.class_id), {d.x1, d.y1, d.x2, d.y2}});
            for (const auto& d : _camera_end_frame_detections[msg.camera_id])
                end_seen.push_back({get_class_name(d.class_id), {d.x1, d.y1, d.x2, d.y2}});
            if (_early.event_ended(msg.camera_id, msg.ts, start_seen, end_seen))
                _save_anchors();

            // Clear the detection list for this camera
            _camera_detections[msg.camera_id].clear();
            _camera_motion_start_time.erase(msg.camera_id);
            _camera_disproven_classes.erase(msg.camera_id);
            _camera_last_processed_ts.erase(msg.camera_id);
            _camera_start_frame_detections.erase(msg.camera_id);
            _camera_end_frame_detections.erase(msg.camera_id);
        }

    } catch (const std::exception& e) {
        R_LOG_ERROR("yolov8_motion_plugin: Failed to process motion event: %s", e.what());
    }
}

std::vector<yolov8_motion_plugin::Detection> yolov8_motion_plugin::detect_persons(const uint8_t* rgb_data, int width, int height, const std::string& camera_id, int64_t timestamp, const r_vss::motion_region& motion_bbox, const std::vector<r_vss::motion_region>& motion_regions, std::vector<Detection>* unfiltered_detections)
{
    std::vector<Detection> detections;

    if (!_initialized || !_net) {
        return detections;
    }

    try {
        // Input is already 640x640 letterboxed RGB from motion engine
        // No resize needed - use directly
        cv::Mat input_img(height, width, CV_8UC3, (void*)rgb_data);
        if (input_img.empty()) {
            return detections;
        }

        // Convert to NCNN mat (RGB input, NCNN expects RGB for PIXEL_RGB)
        ncnn::Mat in = ncnn::Mat::from_pixels(input_img.data, ncnn::Mat::PIXEL_RGB, width, height);
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
                float bw = data[2 * 8400 + i];  // Channel 2, proposal i
                float bh = data[3 * 8400 + i];  // Channel 3, proposal i

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
                // Coordinates are already in 640x640 letterbox space - no transformation needed
                float x1 = cx - bw * 0.5f;
                float y1 = cy - bh * 0.5f;
                float x2 = cx + bw * 0.5f;
                float y2 = cy + bh * 0.5f;

                Detection detection;
                detection.x1 = x1;
                detection.y1 = y1;
                detection.x2 = x2;
                detection.y2 = y2;
                detection.score = max_class_score;
                detection.class_id = max_class_id;
                detection.camera_id = camera_id;
                detection.timestamp = timestamp;

                // Clamp to image bounds (640x640)
                detection.x1 = std::max(0.0f, std::min(639.0f, detection.x1));
                detection.y1 = std::max(0.0f, std::min(639.0f, detection.y1));
                detection.x2 = std::max(0.0f, std::min(639.0f, detection.x2));
                detection.y2 = std::max(0.0f, std::min(639.0f, detection.y2));

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

            // A detection must be spatially explained by motion. Individual connected
            // components prevent the empty space inside a large union box from admitting
            // unrelated parked objects. The union box remains a compatibility fallback.
            const float motion_margin = 32.0f; // pixels in 640x640 space (~5% of frame)

            if(unfiltered_detections)
                *unfiltered_detections = nms_detections;

            for(const auto& det : nms_detections) {
                const float center_x = (det.x1 + det.x2) * 0.5f;
                const float center_y = (det.y1 + det.y2) * 0.5f;
                if(r_vss::motion_evidence_contains_point(
                       motion_bbox, motion_regions, center_x, center_y, motion_margin)) {
                    detections.push_back(det);
                }
            }
        }
        
    } catch (const std::exception& e) {
        R_LOG_ERROR("yolov8_motion_plugin: YOLOv8 detection failed: %s", e.what());
    }
    
    return detections;
}

std::vector<yolov8_motion_plugin::Detection> yolov8_motion_plugin::detect_objects(const uint8_t* rgb_data, int width, int height)
{
    if (!_running)
        return {};
    // No motion evidence: every detection passes the region filter.
    r_vss::motion_region none = {0, 0, 0, 0, false};
    return detect_persons(rgb_data, width, height, std::string(), 0, none, {});
}

void yolov8_motion_plugin::_analyze_and_log_detections(const std::string& camera_id, int64_t end_time_ms)
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

    // Suppress individual static objects, not their entire class. Class-wide
    // suppression loses a moving car whenever a different parked car is present.
    std::vector<Detection> static_anchors;
    {
        const float static_iou_threshold = 0.85f;
        auto sf_it = _camera_start_frame_detections.find(camera_id);
        auto ef_it = _camera_end_frame_detections.find(camera_id);
        if (sf_it != _camera_start_frame_detections.end() && ef_it != _camera_end_frame_detections.end()) {
            for (const auto& sd : sf_it->second) {
                for (const auto& ed : ef_it->second) {
                    if (sd.class_id != ed.class_id) continue;
                    float ix1 = std::max(sd.x1, ed.x1), iy1 = std::max(sd.y1, ed.y1);
                    float ix2 = std::min(sd.x2, ed.x2), iy2 = std::min(sd.y2, ed.y2);
                    if (ix1 < ix2 && iy1 < iy2) {
                        float inter = (ix2 - ix1) * (iy2 - iy1);
                        float a1 = (sd.x2 - sd.x1) * (sd.y2 - sd.y1);
                        float a2 = (ed.x2 - ed.x1) * (ed.y2 - ed.y1);
                        float uni = a1 + a2 - inter;
                        if (uni > 0 && (inter / uni) >= static_iou_threshold) {
                            static_anchors.push_back(sd);
                        }
                    }
                }
            }
        }
    }

    const auto is_static_detection = [&static_anchors](const Detection& detection) {
        const float static_iou_threshold = 0.85f;
        for(const auto& anchor : static_anchors) {
            if(anchor.class_id != detection.class_id) continue;
            const float ix1 = std::max(anchor.x1, detection.x1);
            const float iy1 = std::max(anchor.y1, detection.y1);
            const float ix2 = std::min(anchor.x2, detection.x2);
            const float iy2 = std::min(anchor.y2, detection.y2);
            if(ix1 >= ix2 || iy1 >= iy2) continue;
            const float inter = (ix2 - ix1) * (iy2 - iy1);
            const float anchor_area = (anchor.x2 - anchor.x1) * (anchor.y2 - anchor.y1);
            const float detection_area = (detection.x2 - detection.x1) *
                                         (detection.y2 - detection.y1);
            const float union_area = anchor_area + detection_area - inter;
            if(union_area > 0.0f && inter / union_area >= static_iou_threshold)
                return true;
        }
        return false;
    };

    // Count detections by class for logging (excluding disproven and static classes)
    std::map<int, int> class_counts;
    std::map<int, float> max_confidence;

    for (const auto& detection : it->second) {
        if (disproven.find(detection.class_id) != disproven.end()) continue;
        if (is_static_detection(detection)) continue;
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
        R_LOG_INFO("yolov8_motion_plugin: camera: %s detected: %s (%d frames, max confidence: %.1f%%)",
                   (!maybe_friendly_name.is_null())?maybe_friendly_name.value().c_str():"unknown",
                   class_name, pair.second, confidence * 100);
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
    int64_t first_detection_ts = start_time_ms;
    for (const auto& detection : it->second) {
        if (disproven.find(detection.class_id) != disproven.end()) continue;
        if (is_static_detection(detection)) continue;

        if (!first) {
            json_metadata += ", ";
        }
        if (first) first_detection_ts = detection.timestamp;
        first = false;
        valid_detection_count++;

        auto det_tp = r_time_utils::epoch_millis_to_tp(detection.timestamp);
        std::string det_time_str = r_time_utils::tp_to_iso_8601(det_tp, true);
        const char* class_name = get_class_name(detection.class_id);

        // Format confidence to 3 decimal places
        char conf_buf[32];
        snprintf(conf_buf, sizeof(conf_buf), "%.3f", detection.score);

        char x1_buf[16], y1_buf[16], x2_buf[16], y2_buf[16];
        snprintf(x1_buf, sizeof(x1_buf), "%.4f", detection.x1 / 640.0f);
        snprintf(y1_buf, sizeof(y1_buf), "%.4f", detection.y1 / 640.0f);
        snprintf(x2_buf, sizeof(x2_buf), "%.4f", detection.x2 / 640.0f);
        snprintf(y2_buf, sizeof(y2_buf), "%.4f", detection.y2 / 640.0f);

        json_metadata += R"({"timestamp": ")" + det_time_str +
                        R"(", "class_name": ")" + class_name +
                        R"(", "confidence": )" + conf_buf +
                        R"(, "x1": )" + x1_buf +
                        R"(, "y1": )" + y1_buf +
                        R"(, "x2": )" + x2_buf +
                        R"(, "y2": )" + y2_buf + "}";
    }

    json_metadata += R"(], "total_detections": )" + std::to_string(valid_detection_count) + "}}";

    // Only write metadata and emit detections if we have valid detections
    if (valid_detection_count > 0) {
        auto& stream_keeper = _host->get_stream_keeper();
        stream_keeper.write_metadata(camera_id, "yolov8_motion_plugin", json_metadata, first_detection_ts);

        for (const auto& detection : it->second) {
            if (disproven.find(detection.class_id) != disproven.end()) continue;
            if (is_static_detection(detection)) continue;
            // Already on the bus, sent early.
            if (_early.sent_early(camera_id, get_class_name(detection.class_id), detection.timestamp)) continue;

            r_vss::r_detection det;
            det.camera_id  = camera_id;
            det.ts         = detection.timestamp;
            det.class_name = get_class_name(detection.class_id);
            det.confidence = detection.score;
            det.x1         = detection.x1;
            det.y1         = detection.y1;
            det.x2         = detection.x2;
            det.y2         = detection.y2;
            _host->emit_detection(det);
        }
    }
}

void yolov8_motion_plugin::_early_alerts_for(const std::string& camera_id, const std::vector<Detection>& detections)
{
    for (const auto& d : detections) {
        const char* class_name = get_class_name(d.class_id);
        auto decision = _early.consider(camera_id, class_name, d.score, {d.x1, d.y1, d.x2, d.y2}, d.timestamp);
        switch (decision.verdict) {
            case r_vss::r_early_alerts::early_not_alerting:
            case r_vss::r_early_alerts::early_already_sent:
                continue;
            case r_vss::r_early_alerts::early_below_floor:
                R_LOG_INFO("yolov8_motion_plugin: early %s on %s at %.0f%%: held, under the %.0f%% floor",
                           class_name, camera_id.c_str(), d.score * 100, r_vss::r_early_alerts::MIN_CONFIDENCE * 100);
                continue;
            case r_vss::r_early_alerts::early_anchored:
                R_LOG_INFO("yolov8_motion_plugin: early %s on %s at %.0f%%: held, on a %s static anchor (IoU %.2f)",
                           class_name, camera_id.c_str(), d.score * 100, decision.strong_anchor ? "parked" : "recurring", decision.anchor_iou);
                continue;
            case r_vss::r_early_alerts::early_sent:
                break;
        }

        R_LOG_INFO("yolov8_motion_plugin: early %s on %s at %.0f%%: sent", class_name, camera_id.c_str(), d.score * 100);
        r_vss::r_detection det;
        det.camera_id  = camera_id;
        det.ts         = d.timestamp;
        det.class_name = class_name;
        det.confidence = d.score;
        det.x1         = d.x1;
        det.y1         = d.y1;
        det.x2         = d.x2;
        det.y2         = d.y2;
        _host->emit_detection(det);
    }
}

std::string yolov8_motion_plugin::_anchors_path() const
{
    if (!_host || _host->get_top_dir().empty())
        return std::string();
    return _host->get_top_dir() + PATH_SLASH + "yolov8_static_anchors.json";
}

void yolov8_motion_plugin::_save_anchors()
{
    auto path = _anchors_path();
    if (path.empty())
        return;
    try {
        auto j = _early.to_json();
        auto tmp = path + ".tmp";
        r_fs::write_file((const uint8_t*)j.data(), j.size(), tmp);
        r_fs::atomic_rename_file(tmp, path);
    } catch (const std::exception& e) {
        R_LOG_ERROR("yolov8_motion_plugin: couldn't save static anchors: %s", e.what());
    }
}

const char* yolov8_motion_plugin::get_class_name(int class_id)
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
    yolov8_motion_plugin* plugin = new yolov8_motion_plugin(host_ptr);

    // Return as opaque handle
    return reinterpret_cast<r_motion_plugin_handle>(plugin);
}

R_API void stop_plugin(r_motion_plugin_handle plugin)
{
    // Cast opaque handle back to actual type and stop
    yolov8_motion_plugin* plugin_ptr = reinterpret_cast<yolov8_motion_plugin*>(plugin);
    plugin_ptr->stop();
}

R_API void destroy_plugin(r_motion_plugin_handle plugin)
{
    // Cast opaque handle back to actual type and delete
    yolov8_motion_plugin* plugin_ptr = reinterpret_cast<yolov8_motion_plugin*>(plugin);
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
    yolov8_motion_plugin* plugin_ptr = reinterpret_cast<yolov8_motion_plugin*>(plugin);

    // Convert C types back to C++ types
    std::string camera_id_str(camera_id);
    std::vector<uint8_t> frame_data_vec(frame_data, frame_data + frame_data_size);
    r_vss::motion_region motion_bbox = {motion_x, motion_y, motion_width, motion_height, has_motion};
    r_vss::r_motion_event evt_enum = static_cast<r_vss::r_motion_event>(evt);

    // Call the C++ method
    plugin_ptr->post_motion_event(evt_enum, camera_id_str, ts, frame_data_vec, width, height, motion_bbox);
}

R_API int detect_objects(
    r_motion_plugin_handle plugin,
    const uint8_t* frame_data,
    size_t frame_data_size,
    uint16_t width,
    uint16_t height,
    r_object_detection_cb on_detection,
    void* userdata)
{
    if (!plugin || !frame_data || frame_data_size != (size_t)width * height * 3)
        return -1;
    yolov8_motion_plugin* plugin_ptr = reinterpret_cast<yolov8_motion_plugin*>(plugin);
    try
    {
        for (const auto& d : plugin_ptr->detect_objects(frame_data, width, height))
            on_detection(userdata, yolov8_motion_plugin::class_name(d.class_id), d.score, d.x1, d.y1, d.x2, d.y2);
        return 0;
    }
    catch (const std::exception& e)
    {
        R_LOG_ERROR("yolov8_motion_plugin: detect_objects failed: %s", e.what());
        return -1;
    }
}

R_API void post_motion_event_regions(
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
    bool has_motion,
    const int* motion_regions_xywh,
    size_t motion_region_count)
{
    yolov8_motion_plugin* plugin_ptr = reinterpret_cast<yolov8_motion_plugin*>(plugin);

    std::vector<r_vss::motion_region> motion_regions;
    if(motion_regions_xywh)
    {
        motion_regions.reserve(motion_region_count);
        for(size_t i = 0; i < motion_region_count; ++i)
        {
            const size_t offset = i * 4;
            motion_regions.push_back({motion_regions_xywh[offset],
                                      motion_regions_xywh[offset + 1],
                                      motion_regions_xywh[offset + 2],
                                      motion_regions_xywh[offset + 3],
                                      true});
        }
    }

    std::string camera_id_str(camera_id);
    std::vector<uint8_t> frame_data_vec(frame_data, frame_data + frame_data_size);
    r_vss::motion_region motion_bbox = {motion_x, motion_y, motion_width, motion_height, has_motion};
    r_vss::r_motion_event evt_enum = static_cast<r_vss::r_motion_event>(evt);

    plugin_ptr->post_motion_event(evt_enum, camera_id_str, ts, frame_data_vec, width, height,
                                  motion_bbox, motion_regions);
}

}
