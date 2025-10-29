
#ifndef __vision_frame_h
#define __vision_frame_h

#include <memory>
#include <vector>
#include <cstdint>

namespace vision
{

struct frame
{
    std::shared_ptr<std::vector<uint8_t>> buffer;
    uint16_t w;
    uint16_t h;
    uint16_t original_w;
    uint16_t original_h;
    int64_t pts;  // Presentation timestamp
};

}

#endif
