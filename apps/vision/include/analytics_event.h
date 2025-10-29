#ifndef __vision_analytics_event_h
#define __vision_analytics_event_h

#include <chrono>
#include <string>
#include <vector>

namespace vision
{

struct analytics_event
{
    std::string stream_tag;                           // e.g., "person_plugin"
    std::chrono::system_clock::time_point timestamp;  // When the event occurred
    std::string json_data;                            // Raw JSON data from the analytics
};

}

#endif