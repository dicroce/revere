#ifndef __revere_r_recap_h
#define __revere_r_recap_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <cstddef>
#include <vector>

namespace r_vss
{

// A recap is a short, sped-up MP4 of an event's whole span: a few dozen frames
// picked evenly across it, played at up to 12 fps. It is made from key frames
// only where there are enough of them, since each decodes on its own.

constexpr uint16_t RECAP_WIDTH = 640;
constexpr uint16_t RECAP_HEIGHT = 360;
constexpr size_t RECAP_MIN_FRAMES = 24;
constexpr size_t RECAP_MAX_FRAMES = 96;
constexpr int RECAP_MIN_FPS = 2;
constexpr int RECAP_MAX_FPS = 12;
constexpr int64_t RECAP_MIN_DURATION_MS = 2000;
// A span with too few key frames is decoded in full, if it's at most this long.
constexpr int64_t RECAP_DECODE_ALL_MAX_MS = 60000;
// The longest span a recap covers.
constexpr int64_t RECAP_MAX_SPAN_MS = 60 * 60 * 1000;
constexpr uint32_t RECAP_BITRATE = 600000;
// The most decode() calls for one stored frame, draining what it gives.
constexpr int RECAP_DECODE_CALLS = 8;

struct recap_plan
{
    // false: decode the key frames at times. true: decode every frame from the
    // key frame before the span's start, and keep the first at or after each of times.
    bool decode_all {false};
    std::vector<int64_t> times;
    int fps {RECAP_MIN_FPS};
};

// n of v's values, evenly spread and including the first and last; all of v if it has n or fewer.
R_API std::vector<int64_t> recap_pick_evenly(const std::vector<int64_t>& v, size_t n);

// The frames for a recap of [start_ms, end_ms], from the key frame times in or around it.
// - At least RECAP_MIN_FRAMES key frames in the span: up to RECAP_MAX_FRAMES of them.
// - Fewer, in a span of at most RECAP_DECODE_ALL_MAX_MS: RECAP_MIN_FRAMES times evenly
//   across it from its start, decoding every frame.
// - Fewer, in a longer span: the key frames there are (none means the caller uses the
//   key frame showing at start_ms).
// fps plays the frames in at least RECAP_MIN_DURATION_MS, at RECAP_MIN_FPS to RECAP_MAX_FPS.
R_API recap_plan plan_recap(const std::vector<int64_t>& key_times, int64_t start_ms, int64_t end_ms);

// Copies a YUV420P picture (sw x sh, even) into the middle of a black dw x dh one.
// dst holds dw*dh*3/2 bytes. A picture larger than the canvas is cropped to it.
R_API void recap_pad_yuv420p(const uint8_t* src, uint16_t sw, uint16_t sh, uint8_t* dst, uint16_t dw, uint16_t dh);

}

#endif
