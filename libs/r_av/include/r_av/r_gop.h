#ifndef __r_av_r_gop_h
#define __r_av_r_gop_h

#include <cstdint>

extern "C" {
#include <libavutil/rational.h>
}

namespace r_av
{

/**
 * Frames in a group of pictures that lasts `seconds`, at `framerate`: the encoder's keyframe interval.
 * A whole number of frames, rounded up (a 30000/1001 rate gives 30 a second, not 30000), and never under 1.
 */
inline uint16_t r_gop_frames(AVRational framerate, uint16_t seconds)
{
    if (framerate.num <= 0 || framerate.den <= 0 || seconds == 0)
        return 1;
    const int64_t per_second = ((int64_t)framerate.num + framerate.den - 1) / framerate.den;
    const int64_t frames = per_second * seconds;
    return (uint16_t)(frames > 65535 ? 65535 : (frames < 1 ? 1 : frames));
}

}

#endif
