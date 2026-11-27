#ifndef __r_vss_r_ts_discipline_h
#define __r_vss_r_ts_discipline_h

#include "r_utils/r_macro.h"
#include <atomic>
#include <cstdint>

namespace r_vss
{

// Keeps one camera's recorded timeline on the wall clock.
//
// A frame is stored at stream start + its pts: the wall clock when the stream
// connected, plus the camera's own elapsed time since. Nothing ties the two
// clocks together after that, so a camera whose clock runs a little fast or
// slow carries the recording away from real time for as long as the stream
// stays up: one camera measured 1.2 s an hour, 90 s ahead after three days.
// Events, alerts and playback are all found by that time.
//
// This steers the timeline back: it returns a correction, in ms, to add to
// stream start + pts, and moves it slowly so frame spacing stays the camera's.
// No threads and no clock: the caller passes each video frame's uncorrected
// time and the wall clock when it arrived. Audio adds the same correction
// (correction_ms()), so sound and picture stay as far apart as the camera
// sent them.
//
// The measure: error = corrected time - arrival. A frame can't arrive before
// it was captured, only later (network jitter, a stall), so the highest error
// in each WINDOW_MS is the one from the frame that was delayed least, and that
// is what the correction follows. Each window it takes out error * window /
// TAU_MS, up to a limit. A window only closes when a frame arrives, so after a
// stall it can be any length; it counts as two at most, since nothing more was
// learned in it than in a short one.
//
// The two directions aren't equally sure:
//  - Stored time ahead of arrival can only be the camera's clock running
//    fast, because a frame can't arrive before it was captured. That is taken
//    out at up to MAX_SLEW_PPM of the window: 0.5%, one frame in two hundred,
//    which no viewer sees.
//  - Stored time behind arrival is either a slow camera clock or frames that
//    reached us late: revere behind on this camera, a Wi-Fi stall, a TCP
//    backlog. So the timeline is moved later by at most MAX_LATE_SLEW_PPM of
//    the window, 60 ms a minute. That is three times the worst camera clock
//    measured (1.2 s an hour, about 330 ppm), but a backlog seconds deep
//    moves the timeline only that much for as long as it lasts, and it is
//    taken back out at the faster rate once frames arrive on time.
//  - While the caller says frames are backed up (its lag watchdog sees them
//    arriving behind live), a window doesn't move the timeline later at all.
//    Moving it earlier is still allowed.
//
// A camera clock that jumps isn't steered out, it is re-anchored at once, at
// that frame: pts stepping back more than JUMP_MS (a stream restart, a clock
// set back), or forward more than JUMP_MS further than the wall clock moved
// since the frame before (a clock set forward; a stall can't do that, its
// frames come with the wall clock ahead of them). The frame is stored at its
// arrival time, or 1 ms after the frame before it if that is later.
class r_ts_discipline
{
public:
    enum
    {
        WINDOW_MS = 2000,
        TAU_MS = 30000,
        MAX_SLEW_PPM = 5000,
        MAX_LATE_SLEW_PPM = 1000,
        JUMP_MS = 5000
    };

    // One video frame: raw_ts_ms = stream start + pts, now_ms = the wall clock
    // at arrival, backed_up = frames are reaching us behind live, so this one's
    // lateness says nothing about the camera's clock. Returns the correction
    // to add to raw_ts_ms. One thread.
    R_API int64_t video_frame(int64_t raw_ts_ms, int64_t now_ms, bool backed_up = false);

    // The correction as of the last video frame. Any thread.
    R_API int64_t correction_ms() const { return _published.load(std::memory_order_relaxed); }

    // How many times the camera's clock jumped and the timeline was re-anchored.
    R_API int jumps() const { return _jumps; }

    // True when the last video_frame() re-anchored.
    R_API bool jumped() const { return _jumped; }

private:
    bool _started {false};
    double _correction {0.0};
    std::atomic<int64_t> _published {0};
    int64_t _last_raw_ms {0};
    int64_t _last_now_ms {0};
    int64_t _last_out_ms {0};
    int64_t _window_start_ms {0};
    double _window_max_error {0.0};
    bool _window_has_error {false};
    bool _window_backed_up {false};
    int _jumps {0};
    bool _jumped {false};
};

}

#endif
