
#ifndef __vision_segment_h
#define __vision_segment_h

#include <chrono>
#include <vector>
#include <cstdint>

namespace vision
{

struct segment
{
    std::chrono::system_clock::time_point start;
    std::chrono::system_clock::time_point end;
};

int64_t segment_to_millisecond_duration(const segment& s);

}

#endif
