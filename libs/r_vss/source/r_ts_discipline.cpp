#include "r_vss/r_ts_discipline.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace r_vss;
using namespace std;

int64_t r_ts_discipline::video_frame(int64_t raw_ts_ms, int64_t now_ms, bool backed_up)
{
    _jumped = false;
    _drift_warning = false;

    if(!_started)
    {
        _started = true;
        _window_start_ms = now_ms;
        _anchored_at.store(now_ms, std::memory_order_relaxed);
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
            _window_backed_up = false;
            _jumps.fetch_add(1, std::memory_order_relaxed);
            _anchored_at.store(now_ms, std::memory_order_relaxed);
            _anchor_correction = _correction;
            _drift_warned = 0;
            _jumped = true;
        }
    }

    double error = (double)raw_ts_ms + _correction - (double)now_ms;
    if(!_window_has_error || error > _window_max_error)
    {
        _window_max_error = error;
        _window_has_error = true;
    }

    if(backed_up)
        _window_backed_up = true;

    int64_t window = now_ms - _window_start_ms;
    if(window >= WINDOW_MS)
    {
        double step = _window_max_error * (double)window / (double)TAU_MS;
        // A long window is a stall: no frames, so nothing more was learned
        // than in a short one, and it earns no more slew.
        double counted = (double)(std::min)(window, (int64_t)(2 * WINDOW_MS));
        // step > 0 moves the timeline earlier (the camera's clock is fast,
        // certain); step < 0 moves it later (a slow clock, or frames that
        // came late), so it's slower, and not at all while backed up.
        double earlier = counted * (double)MAX_SLEW_PPM / 1000000.0;
        double later = _window_backed_up ? 0.0 : counted * (double)MAX_LATE_SLEW_PPM / 1000000.0;
        step = (std::max)(-later, (std::min)(earlier, step));
        _correction -= step;
        _window_start_ms = now_ms;
        _window_has_error = false;
        _window_backed_up = false;
    }

    int64_t correction = (int64_t)std::llround(_correction);
    _published.store(correction, std::memory_order_relaxed);

    // The drift since the anchor, and a warning at each new multiple of
    // DRIFT_WARN_MS of it.
    int64_t drift = (int64_t)std::llround(_correction - _anchor_correction);
    _drift.store(drift, std::memory_order_relaxed);
    int64_t steps = (drift < 0 ? -drift : drift) / DRIFT_WARN_MS;
    if(steps > _drift_warned)
    {
        _drift_warned = steps;
        _drift_warning = true;
    }

    _last_raw_ms = raw_ts_ms;
    _last_now_ms = now_ms;
    _last_out_ms = raw_ts_ms + correction;

    return correction;
}

string r_vss::drift_message(int64_t drift_ms, int64_t over_ms)
{
    const double secs = (double)(drift_ms < 0 ? -drift_ms : drift_ms) / 1000.0;
    const double hours = (double)(over_ms > 0 ? over_ms : 0) / 3600000.0;
    char buf[256];
    if(hours >= 0.1)
        snprintf(buf, sizeof(buf), "the camera's clock is %.1f s %s of ours after %.1f h (about %.1f s an hour); "
                 "the recording is kept on our clock; check that the camera takes its time from the internet (NTP)",
                 secs, drift_ms < 0 ? "fast" : "slow", hours, secs / hours);
    else
        snprintf(buf, sizeof(buf), "the camera's clock is %.1f s %s of ours; "
                 "the recording is kept on our clock; check that the camera takes its time from the internet (NTP)",
                 secs, drift_ms < 0 ? "fast" : "slow");
    return buf;
}

nlohmann::json r_vss::recording_clock_json(bool receiving_video, int64_t drift_ms, int64_t anchored_at_ms, int jumps)
{
    if(!receiving_video || anchored_at_ms <= 0)
        return nullptr;
    return {{"drift_ms", drift_ms}, {"since_ms", anchored_at_ms}, {"jumps", jumps}};
}
