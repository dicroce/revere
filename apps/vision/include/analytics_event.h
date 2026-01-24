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