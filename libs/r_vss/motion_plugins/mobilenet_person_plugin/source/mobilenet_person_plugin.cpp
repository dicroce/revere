#include "mobilenet_person_plugin.h"
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

mobilenet_person_plugin::mobilenet_person_plugin(r_vss::r_motion_event_plugin_host* host)
    : _host(host),
      _initialized(false),
      _running(false)
{
    R_LOG_INFO("mobilenet_person_plugin: Constructor called");
    
    try {
        auto working_directory = r_fs::working_directory();

        auto params_path = working_directory + "/models/mobilenet_person/mobilenet_ssd.param";
        auto bin_path = working_directory + "/models/mobilenet_person/mobilenet_ssd.bin";

        R_LOG_INFO("mobilenet_person_plugin: Loading MobileNet SSD model from %s", params_path.c_str());

        // Create NCNN network instance
        _net = std::make_unique<ncnn::Net>();
        
        // Configure NCNN options BEFORE loading model
        _net->opt.use_vulkan_compute = false;  // Use CPU for now
        _net->opt.num_threads = ncnn::get_big_cpu_count();
        _net->opt.use_fp16_storage = false;
        _net->opt.use_fp16_arithmetic = false;
        
        // Log NCNN information
        R_LOG_INFO("mobilenet_person_plugin: NCNN initialized with %d threads", _net->opt.num_threads);
        
        // Load the model
        int ret_param = _net->load_param(params_path.c_str());
        if (ret_param != 0) {
            R_THROW(("Failed to load model parameters from %s", params_path.c_str()));
        }
        
        int ret_bin = _net->load_model(bin_path.c_str());
        if (ret_bin != 0) {
            R_THROW(("Failed to load model weights from %s", bin_path.c_str()));
        }
        
        _initialized = true;
        R_LOG_INFO("mobilenet_person_plugin: Successfully loaded MobileNet SSD model");
        
        // Start worker thread
        _running = true;
        _thread = std::thread(&mobilenet_person_plugin::_entry_point, this);
        R_LOG_INFO("mobilenet_person_plugin: Worker thread started");
        
    }
    catch (const std::exception& e) {
        R_LOG_ERROR("mobilenet_person_plugin: Failed to initialize NCNN: %s", e.what());
    }
}

mobilenet_person_plugin::~mobilenet_person_plugin()
{
    // Stop worker thread
    if (_running) {
        _running = false;
        _event_queue.wake();
        if (_thread.joinable()) {
            _thread.join();
        }
        R_LOG_INFO("mobilenet_person_plugin: Worker thread stopped");
    }
    
    if (_net) {
        _net->clear();
        R_LOG_INFO("mobilenet_person_plugin: NCNN network cleared");
    }
}

void mobilenet_person_plugin::post_motion_event(r_vss::r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height)
{
    // Queue all motion events for processing
    MotionEventMessage msg;
    msg.evt = evt;
    msg.camera_id = camera_id;
    msg.ts = ts;
    msg.frame_data = frame_data;
    msg.width = width;
    msg.height = height;
    
    try {
        _event_queue.post(msg);
    } catch (const std::exception& e) {
        R_LOG_ERROR("mobilenet_person_plugin: Failed to queue motion event: %s", e.what());
    }
}

// Pure C API implementation - no C++ types in function signatures
extern "C"
{

R_API r_motion_plugin_handle load_plugin(r_motion_event_plugin_host_handle host)
{
    R_LOG_INFO("mobilenet_person_plugin: load_plugin() called");

    // Cast opaque handle back to actual type
    r_vss::r_motion_event_plugin_host* host_ptr = reinterpret_cast<r_vss::r_motion_event_plugin_host*>(host);

    // Create plugin instance
    mobilenet_person_plugin* plugin = new mobilenet_person_plugin(host_ptr);

    // Return as opaque handle
    return reinterpret_cast<r_motion_plugin_handle>(plugin);
}

R_API void destroy_plugin(r_motion_plugin_handle plugin)
{
    // Cast opaque handle back to actual type and delete
    mobilenet_person_plugin* plugin_ptr = reinterpret_cast<mobilenet_person_plugin*>(plugin);
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
    mobilenet_person_plugin* plugin_ptr = reinterpret_cast<mobilenet_person_plugin*>(plugin);

    // Convert C types back to C++ types
    std::string camera_id_str(camera_id);
    std::vector<uint8_t> frame_data_vec(frame_data, frame_data + frame_data_size);
    r_vss::motion_region motion_bbox = {motion_x, motion_y, motion_width, motion_height, has_motion};
    r_vss::r_motion_event evt_enum = static_cast<r_vss::r_motion_event>(evt);

    // Call the C++ method
    plugin_ptr->post_motion_event(evt_enum, camera_id_str, ts, frame_data_vec, width, height, motion_bbox);
}

}

void mobilenet_person_plugin::_entry_point()
{
    R_LOG_INFO("mobilenet_person_plugin: Worker thread started processing events");
    
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
            R_LOG_ERROR("mobilenet_person_plugin: Error in worker thread: %s", e.what());
        }
    }
    
    R_LOG_INFO("mobilenet_person_plugin: Worker thread exiting");
}

void mobilenet_person_plugin::_process_motion_event(const MotionEventMessage& msg)
{
    if (!_initialized || !_host) {
        R_LOG_WARNING("mobilenet_person_plugin: Plugin not initialized, skipping event");
        return;
    }
    
    try {
        if (msg.evt == r_vss::motion_event_start) {
            // Clear any existing detections for this camera (handle missing end events)
            _camera_detections[msg.camera_id].clear();
        }
        
        if (msg.evt == r_vss::motion_event_start || msg.evt == r_vss::motion_event_update) {
            // Use actual frame data - calculate actual dimensions from frame size
            size_t actual_pixels = msg.frame_data.size() / 3; // BGR = 3 bytes per pixel
            
            // Calculate actual width and height from frame data
            // Since we know height should be close to what we requested, use that to derive width
            uint16_t actual_width = (uint16_t)(actual_pixels / msg.height);
            uint16_t actual_height = msg.height;
            
            size_t expected_bgr_size = actual_width * actual_height * 3;
            
            if (msg.frame_data.size() == expected_bgr_size) {
                // Create 300x300 BGR bitmap with black background
                const uint16_t target_width = 300;
                const uint16_t target_height = 300;
                std::vector<uint8_t> bgr_300x300(target_width * target_height * 3, 0);
                
                // Calculate centering offsets using actual dimensions
                uint16_t offset_x = (target_width - actual_width) / 2;
                uint16_t offset_y = (target_height - actual_height) / 2;
                
                // Copy BGR data to centered position in 300x300 bitmap
                for (uint16_t y = 0; y < actual_height; y++) {
                    for (uint16_t x = 0; x < actual_width; x++) {
                        // Source BGR pixel
                        const uint8_t* src_pixel = &msg.frame_data[(y * actual_width + x) * 3];
                        
                        // Target BGR pixel (centered)
                        uint16_t target_x = x + offset_x;
                        uint16_t target_y = y + offset_y;
                        uint8_t* dst_pixel = &bgr_300x300[(target_y * target_width + target_x) * 3];
                        dst_pixel[0] = src_pixel[0]; // B
                        dst_pixel[1] = src_pixel[1]; // G
                        dst_pixel[2] = src_pixel[2]; // R
                    }
                }
                
                // Run object detection on centered 300x300 BGR frame
                auto detections = detect_persons(bgr_300x300.data(), target_width, target_height, msg.camera_id, msg.ts);
                
                // Append all detections to the camera's list
                _camera_detections[msg.camera_id].insert(_camera_detections[msg.camera_id].end(), detections.begin(), detections.end());
            } else {
                R_LOG_ERROR("mobilenet_person_plugin: Frame size mismatch for camera %s - calculated %dx%d (%zu bytes), got %zu bytes", 
                           msg.camera_id.c_str(), actual_width, actual_height, expected_bgr_size, msg.frame_data.size());
            }
        }
        
        if (msg.evt == r_vss::motion_event_end) {
            // Analyze all detections for this motion sequence and log results
            _analyze_and_log_detections(msg.camera_id);
            
            // Clear the detection list for this camera
            _camera_detections[msg.camera_id].clear();
        }
        
    } catch (const std::exception& e) {
        R_LOG_ERROR("mobilenet_person_plugin: Failed to process motion event: %s", e.what());
    }
}

std::vector<mobilenet_person_plugin::Detection> mobilenet_person_plugin::detect_persons(const uint8_t* bgr_data, int width, int height, const std::string& camera_id, int64_t timestamp)
{
    std::vector<Detection> detections;
    
    if (!_initialized || !_net) {
        R_LOG_ERROR("mobilenet_person_plugin: Network not initialized");
        return detections;
    }
    
    try {
        // MobileNet SSD preprocessing: letterbox to 300x300 to maintain aspect ratio
        const int model_width = 300;
        const int model_height = 300;
        
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
            R_LOG_ERROR("mobilenet_person_plugin: Failed to create OpenCV Mat from BGR data");
            return detections;
        }
        
        cv::Mat resized_img;
        cv::resize(original_img, resized_img, cv::Size(scaled_width, scaled_height));
        if (resized_img.empty()) {
            R_LOG_ERROR("mobilenet_person_plugin: Failed to resize image");
            return detections;
        }
        
        // Create padded image (letterbox)
        cv::Mat letterbox_img = cv::Mat::zeros(model_height, model_width, CV_8UC3);
        cv::Rect roi(pad_x, pad_y, scaled_width, scaled_height);
        resized_img.copyTo(letterbox_img(roi));
        
        if (letterbox_img.empty()) {
            R_LOG_ERROR("mobilenet_person_plugin: Failed to create letterbox image");
            return detections;
        }
        
        // Convert to NCNN mat
        ncnn::Mat in = ncnn::Mat::from_pixels(letterbox_img.data, ncnn::Mat::PIXEL_BGR, model_width, model_height);
        if (in.empty()) {
            R_LOG_ERROR("mobilenet_person_plugin: Failed to create NCNN Mat from letterbox image");
            return detections;
        }
        
        // MobileNet SSD normalization: mean subtraction [127.5, 127.5, 127.5] and scale 1/127.5
        const float mean_vals[3] = {127.5f, 127.5f, 127.5f};
        const float norm_vals[3] = {1.0f/127.5f, 1.0f/127.5f, 1.0f/127.5f};
        in.substract_mean_normalize(mean_vals, norm_vals);
        
        // Create extractor and run inference
        ncnn::Extractor ex = _net->create_extractor();
        
        // MobileNet SSD input layer name is "data"
        int ret = ex.input("data", in);
        if (ret != 0) {
            R_LOG_ERROR("mobilenet_person_plugin: Failed to set input data (ret=%d)", ret);
            return detections;
        }
        
        // MobileNet SSD output layer name is "detection_out"
        ncnn::Mat out;
        ret = ex.extract("detection_out", out);
        if (ret != 0) {
            R_LOG_ERROR("mobilenet_person_plugin: Failed to extract detection_out (ret=%d)", ret);
            return detections;
        }
        
        // Check if output is empty (no detections found)
        if (out.empty()) {
            return detections;  // Empty detections is valid - nothing found
        }
        
        // MobileNet SSD post-processing
        // NCNN DetectionOutput format: [6, N] where each detection is:
        // [class_id, confidence, xmin, ymin, xmax, ymax]
        
        const float conf_threshold = 0.5f; // Additional filtering (model already filters at 0.45)
        
        // NCNN DetectionOutput format: w=6 (attributes), h=N (detections), c=1
        // Special case: w=0 h=0 c=0 means no detections above threshold
        if (out.w == 0 && out.h == 0 && out.c == 0) {
            R_LOG_DEBUG("mobilenet_person_plugin: No detections above confidence threshold");
            return detections;
        }
        
        if (out.w != 6 || out.c != 1) {
            R_LOG_ERROR("mobilenet_person_plugin: Unexpected SSD output format: w=%d h=%d c=%d (expected w=6, c=1)", out.w, out.h, out.c);
            return detections;
        }
        
        int num_detections = out.h;
        
        for (int i = 0; i < num_detections; i++) {
            const float* det = out.row(i);
            
            int class_id = (int)det[0];
            float confidence = det[1];
            // Scale coordinates back to original image size
            // NCNN coordinates are normalized (0-1), convert to letterbox pixel coordinates
            float letterbox_x1 = det[2] * model_width;
            float letterbox_y1 = det[3] * model_height;
            float letterbox_x2 = det[4] * model_width;
            float letterbox_y2 = det[5] * model_height;
            
            // Then convert from letterbox space to original image space
            float x1 = (letterbox_x1 - pad_x) / scale;
            float y1 = (letterbox_y1 - pad_y) / scale;
            float x2 = (letterbox_x2 - pad_x) / scale;
            float y2 = (letterbox_y2 - pad_y) / scale;
            
            // Skip logging individual detections
            
            // Keep all detections above confidence threshold (not just persons)
            if (confidence > conf_threshold) {
                Detection detection;
                detection.x1 = x1;
                detection.y1 = y1;
                detection.x2 = x2;
                detection.y2 = y2;
                detection.score = confidence;
                detection.class_id = class_id;
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
                }
            }
        }
        
    } catch (const std::exception& e) {
        R_LOG_ERROR("mobilenet_person_plugin: MobileNet SSD detection failed: %s", e.what());
    }
    
    return detections;
}

void mobilenet_person_plugin::_analyze_and_log_detections(const std::string& camera_id)
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
        R_LOG_INFO("mobilenet_person_plugin: camera: %s detected: %s (%d frames, max confidence: %.1f%%)", 
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
    stream_keeper.write_metadata(camera_id, "mobilenet_person_plugin", json_metadata, motion_timestamp);
    
    R_LOG_DEBUG("mobilenet_person_plugin: Analyzed %d total detections for camera %s", 
                (int)it->second.size(), camera_id.c_str());
}

const char* mobilenet_person_plugin::get_class_name(int class_id)
{
    // VOC dataset class names (21 classes including background)
    static const char* class_names[] = {
        "background", "aeroplane", "bicycle", "bird", "boat", "bottle", "bus", "car", "cat", "chair",
        "cow", "diningtable", "dog", "horse", "motorbike", "person", "pottedplant", "sheep", "sofa", "train", "tvmonitor"
    };
    
    const int num_classes = sizeof(class_names) / sizeof(class_names[0]);
    
    if (class_id >= 0 && class_id < num_classes) {
        return class_names[class_id];
    }
    return "unknown";
}
