#include "r_vss/r_ts_discipline.h"
#include <algorithm>
#include <cmath>

using namespace r_vss;
using namespace std;

int64_t r_ts_discipline::video_frame(int64_t raw_ts_ms, int64_t now_ms)
{
    _jumped = false;

    if(!_started)
    {
        _started = true;
        _window_start_ms = now_ms;
    }
    else
    {
        int64_t pts_step = raw_ts_ms - _last_raw_ms;
        int64_t wall_step = now_ms - _last_now_ms;
        if(pts_step < -JUMP_MS || pts_step - wall_step > JUMP_MS)
        {
            // The camera's clock jumped: store this frame at its arrival, but
            // never at or before the frame stored last.
            int64_t at = (std::max)(now_ms, _last_out_ms + 1);
            _correction = (double)(at - raw_ts_ms);
            _window_start_ms = now_ms;
            _window_has_error = false;
            ++_jumps;
            _jumped = true;
        }
    }

    double error = (double)raw_ts_ms + _correction - (double)now_ms;
    if(!_window_has_error || error > _window_max_error)
    {
        _window_max_error = error;
        _window_has_error = true;
    }

    int64_t window = now_ms - _window_start_ms;
    if(window >= WINDOW_MS)
    {
        double step = _window_max_error * (double)window / (double)TAU_MS;
        // A long window is a stall: no frames, so nothing more was learned
        // than in a short one, and it earns no more slew.
        double limit = (double)(std::min)(window, (int64_t)(2 * WINDOW_MS)) * (double)MAX_SLEW_PPM / 1000000.0;
        step = (std::max)(-limit, (std::min)(limit, step));
        _correction -= step;
        _window_start_ms = now_ms;
        _window_has_error = false;
    }

    int64_t correction = (int64_t)std::llround(_correction);
    _published.store(correction, std::memory_order_relaxed);

    _last_raw_ms = raw_ts_ms;
    _last_now_ms = now_ms;
    _last_out_ms = raw_ts_ms + correction;

    return correction;
}
