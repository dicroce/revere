#include "r_vss/r_recap.h"
#include <algorithm>
#include <cstring>

using namespace std;

namespace r_vss
{

vector<int64_t> recap_pick_evenly(const vector<int64_t>& v, size_t n)
{
    if(v.size() <= n)
        return v;
    if(n == 1)
        return {v.front()};

    vector<int64_t> out;
    out.reserve(n);
    const size_t last = v.size() - 1;
    for(size_t i = 0; i < n; ++i)
        out.push_back(v[(i * last + (n - 1) / 2) / (n - 1)]);
    return out;
}

recap_plan plan_recap(const vector<int64_t>& key_times, int64_t start_ms, int64_t end_ms)
{
    recap_plan plan;

    vector<int64_t> in_span;
    for(auto t : key_times)
        if(t >= start_ms && t <= end_ms)
            in_span.push_back(t);

    if(in_span.size() >= RECAP_MIN_FRAMES)
        plan.times = recap_pick_evenly(in_span, RECAP_MAX_FRAMES);
    else if(end_ms > start_ms && end_ms - start_ms <= RECAP_DECODE_ALL_MAX_MS)
    {
        plan.decode_all = true;
        const int64_t span = end_ms - start_ms;
        const int64_t n = (int64_t)RECAP_MIN_FRAMES;
        for(int64_t i = 0; i < n; ++i)
            plan.times.push_back(start_ms + (span * i) / n);
    }
    else
        plan.times = in_span;

    const int64_t frames = max<int64_t>(1, (int64_t)plan.times.size());
    const int64_t fps = (frames * 1000) / RECAP_MIN_DURATION_MS;
    plan.fps = (int)clamp<int64_t>(fps, RECAP_MIN_FPS, RECAP_MAX_FPS);
    return plan;
}

void recap_pad_yuv420p(const uint8_t* src, uint16_t sw, uint16_t sh, uint8_t* dst, uint16_t dw, uint16_t dh)
{
    const size_t dst_y = (size_t)dw * dh;
    const size_t dst_c = dst_y / 4;
    memset(dst, 16, dst_y);              // black luma
    memset(dst + dst_y, 128, dst_c * 2); // neutral chroma

    const uint16_t cw = min(sw, dw), ch = min(sh, dh);
    // Even offsets keep the chroma planes aligned with the luma.
    const size_t ox = ((dw - cw) / 2) & ~1u, oy = ((dh - ch) / 2) & ~1u;
    const size_t sx = ((sw - cw) / 2) & ~1u, sy = ((sh - ch) / 2) & ~1u;

    const uint8_t* s_y = src;
    const uint8_t* s_u = src + (size_t)sw * sh;
    const uint8_t* s_v = s_u + ((size_t)sw * sh) / 4;
    uint8_t* d_y = dst;
    uint8_t* d_u = dst + dst_y;
    uint8_t* d_v = d_u + dst_c;

    for(size_t r = 0; r < ch; ++r)
        memcpy(d_y + (oy + r) * dw + ox, s_y + (sy + r) * sw + sx, cw);
    for(size_t r = 0; r < (size_t)ch / 2; ++r)
    {
        memcpy(d_u + (oy / 2 + r) * (dw / 2) + ox / 2, s_u + (sy / 2 + r) * (sw / 2) + sx / 2, cw / 2);
        memcpy(d_v + (oy / 2 + r) * (dw / 2) + ox / 2, s_v + (sy / 2 + r) * (sw / 2) + sx / 2, cw / 2);
    }
}

}
