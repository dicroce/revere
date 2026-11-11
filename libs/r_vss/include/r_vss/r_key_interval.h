#ifndef __r_vss_r_key_interval_h
#define __r_vss_r_key_interval_h

#include "r_utils/r_macro.h"
#include <atomic>
#include <cstdint>

namespace r_vss
{

// How far apart a stream's key frames come: the longest of the last few gaps (a camera's GOP can vary, and motion
// detection runs on key frames, so the longest is the cadence it can rely on). record() is called by the stream's
// source thread at each key frame; interval_ms() can be read from any thread.
class r_key_interval final
{
public:
    // The gaps looked at.
    static constexpr int GAPS = 4;

    // A key frame arrived at now_ms (steady clock).
    R_API void record(int64_t now_ms)
    {
        if(_last_ms >= 0)
        {
            _gaps[_next++ % GAPS] = now_ms - _last_ms;
            if(_count < GAPS)
                ++_count;
            int64_t longest = 0;
            for(int i = 0; i < _count; ++i)
                longest = _gaps[i] > longest ? _gaps[i] : longest;
            _interval = longest;
        }
        _last_ms = now_ms;
    }

    // The longest of the last GAPS gaps between key frames, in ms; negative until two key frames have been seen.
    R_API int64_t interval_ms() const { return _interval.load(); }

private:
    int64_t _last_ms {-1};
    int64_t _gaps[GAPS] {0, 0, 0, 0};
    int _next {0};
    int _count {0};
    std::atomic<int64_t> _interval {-1};
};

}

#endif
