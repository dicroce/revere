#ifndef __yolov8_person_plugin_h
#define __yolov8_person_plugin_h

#include "r_vss/r_motion_plugin.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_blocking_q.h"
#include <memory>
#include <vector>
#include <thread>
#include <map>
#include <list>

namespace ncnn {
    class Net;
}

namespace r_vss {
    class r_motion_event_plugin_host;
}

class yolov8_person_plugin : public r_vss::r_motion_plugin
{
public:
    struct Detection {
        float x1, y1, x2, y2;  // Bounding box coordinates
        float score;           // Confidence score
        int class_id;          // Class ID
        std::string camera_id; // Camera ID
        int64_t timestamp;     // Timestamp
    };

    struct MotionEventMessage {
        r_vss::r_motion_event evt;
        std::string camera_id;
        int64_t ts;
        std::vector<uint8_t> frame_data;
        uint16_t width;
        uint16_t height;
        r_vss::motion_region motion_bbox;
    };

    R_API yolov8_person_plugin(r_vss::r_motion_event_plugin_host* host);
    R_API virtual ~yolov8_person_plugin();
    
    R_API virtual void post_motion_event(r_vss::r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height, const r_vss::motion_region& motion_bbox) override;

private:
    r_vss::r_motion_event_plugin_host* _host;
    std::unique_ptr<ncnn::Net> _net;
    bool _initialized;
    bool _running;
    std::thread _thread;
    r_utils::r_blocking_q<MotionEventMessage> _event_queue;
    std::map<std::string, std::list<Detection>> _camera_detections;
    
    void _entry_point();
    void _process_motion_event(const MotionEventMessage& msg);
    std::vector<Detection> detect_persons(const uint8_t* rgb_data, int width, int height, const std::string& camera_id, int64_t timestamp, const r_vss::motion_region& motion_bbox);
    void _analyze_and_log_detections(const std::string& camera_id);
    static const char* get_class_name(int class_id);
};

#endif