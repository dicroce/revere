#ifndef __vision_analytics_event_h
#define __vision_analytics_event_h

#include <chrono>
#include <string>
#include <vector>

namespace vision
{

struct analytics_detection
{
    std::string class_name;
    float confidence;
    std::chrono::system_clock::time_point timestamp;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // normalized 0-1 (letterbox coords / 640)
};

struct analytics_event
{
    std::chrono::system_clock::time_point motion_start_time;
    std::chrono::system_clock::time_point motion_end_time;
    std::vector<analytics_detection> detections;
    int total_detections;
};

}

#endif