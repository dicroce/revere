
#ifndef __vision_motion_event_h
#define __vision_motion_event_h

#include <chrono>
#include <vector>
#include <cstdint>

namespace vision
{

struct motion_event
{
    std::chrono::system_clock::time_point start;
    std::chrono::system_clock::time_point end;
    uint8_t motion;
    uint8_t stddev;
};

int64_t motion_event_to_millisecond_duration(const motion_event& s);

}

#endif
