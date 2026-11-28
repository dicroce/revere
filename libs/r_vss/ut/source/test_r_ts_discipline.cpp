
#include "test_r_ts_discipline.h"
#include "r_vss/r_ts_discipline.h"
#include "r_vss/r_lag_watchdog.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_ts_discipline);

typedef r_ts_discipline D;

static const int64_t SEC = 1000;
static const int64_t MIN = 60 * SEC;
static const int64_t HOUR = 60 * MIN;

namespace
{

// A simulated camera stream. Real time advances one frame interval per frame;
// the camera's clock advances the same times `clock_rate` (1.0004 = 400 ppm
// fast). A frame arrives when it was captured plus `delay_ms` (a backlog) plus
// up to `jitter_ms` of network jitter that doesn't accumulate. As the recorder
// does, the first frame's time is the wall clock when it arrived, and a lag
// watchdog says when frames are backed up (`watch_lag` false leaves it out).
struct stream
{
    D d;
    r_lag_watchdog lag;
    bool watch_lag {true};
    double wall {1795000000000.0};   // capture time of the next frame
    double cam {0};                  // camera clock (pts), ms
    double interval {1000.0 / 30};
    double clock_rate {1.0};
    double delay_ms {0};
    int64_t jitter_ms {0};
    uint32_t rng {12345};

    bool anchored {false};
    int64_t stream_start {0};
    int64_t last_arrival {0};
    int64_t last_stored {0};
    int64_t last_raw {0};
    bool have_last {false};
    bool went_back {false};          // a frame was stored at or before the one before it
    int64_t worst_spacing_error {0}; // most a frame's spacing differed from the camera's, ms
    int64_t worst_error {0};         // furthest a frame was stored from when it arrived
    int64_t last_capture_error {0};  // last frame's stored time - when it was captured
    int64_t worst_capture_error {0}; // furthest a frame was stored from when it was captured
    double capture_error_sum {0};
    int64_t frames {0};
    int64_t first_arrival {0};
    std::vector<int64_t> drift_warnings;   // the drift at each frame that warned

    // Sends one frame; returns how far from its arrival it was stored (+ = ahead of the wall clock).
    int64_t frame(bool track = true)
    {
        int64_t jitter = 0;
        if(jitter_ms > 0)
        {
            rng = rng * 1664525u + 1013904223u;
            jitter = (int64_t)((rng >> 8) % (uint32_t)(jitter_ms + 1));
        }
        int64_t arrival = (int64_t)(wall + delay_ms) + jitter;
        if(arrival < last_arrival)
            arrival = last_arrival;   // TCP delivers in order
        last_arrival = arrival;

        int64_t pts = (int64_t)cam;
        if(!anchored)
        {
            stream_start = arrival - pts;
            anchored = true;
        }
        int64_t raw = stream_start + pts;
        bool backed_up = false;
        if(watch_lag)
        {
            lag.frame(pts, arrival);
            backed_up = lag.lag_ms() > r_lag_watchdog::LAG_MS;
        }
        int64_t stored = raw + d.video_frame(raw, arrival, backed_up);
        if(first_arrival == 0)
            first_arrival = arrival;
        if(d.drift_warning())
            drift_warnings.push_back(d.drift_ms());

        if(have_last && !d.jumped())
        {
            if(stored <= last_stored)
                went_back = true;
            int64_t spacing_error = llabs((stored - last_stored) - (raw - last_raw));
            if(spacing_error > worst_spacing_error)
                worst_spacing_error = spacing_error;
        }
        have_last = true;
        last_stored = stored;
        last_raw = raw;

        int64_t error = stored - arrival;
        if(track && llabs(error) > worst_error)
            worst_error = llabs(error);
        last_capture_error = stored - (int64_t)wall;
        if(track)
        {
            worst_capture_error = (std::max)(worst_capture_error, (int64_t)llabs(last_capture_error));
            capture_error_sum += (double)last_capture_error;
            ++frames;
        }

        wall += interval;
        cam += interval * clock_rate;
        return error;
    }

    int64_t run(int64_t ms, bool track = true)
    {
        int64_t error = 0;
        for(double end = wall + (double)ms; wall < end;)
            error = frame(track);
        return error;
    }
};

}

// The camera this was written for: its clock gains 1.2 s an hour, and the
// recording was 90 s ahead after three days. Corrected, it never leaves the
// wall clock.
void test_r_ts_discipline::test_fast_camera_clock()
{
    stream s;
    s.clock_rate = 1.0 + 400e-6;
    int64_t error = s.run(72 * HOUR);
    RTF_ASSERT(llabs(error) < 50);
    RTF_ASSERT(s.worst_error < 50);
    RTF_ASSERT(!s.went_back);
    RTF_ASSERT(s.d.jumps() == 0);

    // What it took out: three days at 400 ppm.
    RTF_ASSERT(s.d.correction_ms() < -100 * SEC);
}

void test_r_ts_discipline::test_slow_camera_clock()
{
    stream s;
    s.clock_rate = 1.0 - 400e-6;
    int64_t error = s.run(72 * HOUR);
    RTF_ASSERT(llabs(error) < 50);
    RTF_ASSERT(s.worst_error < 50);
    RTF_ASSERT(!s.went_back);

    // A bad one: 0.08% slow, 2.9 s an hour, still inside what the timeline
    // may be moved later by (0.1%).
    stream t;
    t.clock_rate = 1.0 - 800e-6;
    t.run(24 * HOUR);
    RTF_ASSERT(t.worst_error < 150);
    RTF_ASSERT(t.worst_capture_error < 150);
}

// Jitter delays frames and never brings one early. The correction follows the
// least delayed frames, so it settles inside the jitter and stays there.
void test_r_ts_discipline::test_jitter()
{
    stream s;
    s.jitter_ms = 80;
    s.run(10 * MIN, false);
    int64_t settled = s.d.correction_ms();
    RTF_ASSERT(llabs(settled) <= 85);

    int64_t lo = settled, hi = settled;
    for(int i = 0; i < 30 * 60 * 60; ++i)
    {
        s.frame();
        lo = (std::min)(lo, s.d.correction_ms());
        hi = (std::max)(hi, s.d.correction_ms());
    }
    RTF_ASSERT(hi - lo <= 10);
    RTF_ASSERT(!s.went_back);

    // Stored at capture time, give or take: the least delayed frames are the
    // ones that tell when frames were captured.
    RTF_ASSERT(llabs((int64_t)(s.capture_error_sum / (double)s.frames)) < 10);
}

// Frame spacing stays the camera's: the correction moves at most 0.5%, so
// across one 2 s window a frame's spacing changes by 10 ms at most.
void test_r_ts_discipline::test_frame_spacing()
{
    stream s;
    s.clock_rate = 1.0 + 2000e-6;
    s.jitter_ms = 40;
    s.run(2 * HOUR);
    RTF_ASSERT(s.worst_spacing_error <= 11);
    RTF_ASSERT(!s.went_back);
}

namespace
{

// Nothing for 8 s, then the backlog arrives at once. Returns how far the
// correction moved, and leaves the stream live again.
int64_t stall_then_backlog(stream& s)
{
    s.run(5 * MIN);
    int64_t before = s.d.correction_ms();

    // Captured over the next 8 s, all delivered when the link comes back.
    double stall_end = s.wall + 8.0 * SEC;
    int64_t most_moved = 0;
    while(s.wall < stall_end)
    {
        s.delay_ms = stall_end - s.wall;
        s.frame(false);
        most_moved = (std::max)(most_moved, (int64_t)llabs(s.d.correction_ms() - before));
    }
    s.delay_ms = 0;
    for(int i = 0; i < 30 * 30; ++i)
    {
        s.frame(false);
        most_moved = (std::max)(most_moved, (int64_t)llabs(s.d.correction_ms() - before));
    }
    return most_moved;
}

}

// A Wi-Fi stall. The frames are late, not the camera's clock wrong, so the
// timeline barely moves and is back once the stream is live again. With the
// lag watchdog saying they're backed up, it doesn't move at all; without it,
// the 8 s window counts as two, at 0.1%.
void test_r_ts_discipline::test_stall_then_backlog()
{
    stream s;
    int64_t moved = stall_then_backlog(s);
    RTF_ASSERT(moved <= 1);
    RTF_ASSERT(s.d.jumps() == 0);
    RTF_ASSERT(!s.went_back);
    int64_t error = s.run(3 * MIN, false);
    RTF_ASSERT(llabs(error) <= 10);

    stream t;
    t.watch_lag = false;
    moved = stall_then_backlog(t);
    RTF_ASSERT(moved <= 10);
    RTF_ASSERT(t.d.jumps() == 0);
    RTF_ASSERT(!t.went_back);
    error = t.run(3 * MIN, false);
    RTF_ASSERT(llabs(error) <= 10);
}

// On connect a camera sends what it has buffered at once: a second of frames
// in a few ms. The first of them is stored at its arrival, so the live frames
// after them start a second ahead of the wall clock; that is steered out.
void test_r_ts_discipline::test_connect_burst()
{
    stream s;
    s.delay_ms = 1000;
    for(int i = 0; i < 30; ++i)
    {
        s.frame(false);
        s.delay_ms -= s.interval;   // each buffered frame is 33 ms newer and arrives with the others
    }
    s.delay_ms = 0;
    int64_t at_first = s.frame(false);
    RTF_ASSERT(at_first > 900);

    int64_t error = s.run(5 * MIN, false);
    RTF_ASSERT(llabs(error) < 50);
    RTF_ASSERT(!s.went_back);
    RTF_ASSERT(s.d.jumps() == 0);
}

// The camera's clock is set back three hours mid-stream (seen in the field as
// a silent pts jump with no reconnect). The frame is stored at its arrival and
// the timeline carries on from there.
void test_r_ts_discipline::test_clock_jumps_back()
{
    stream s;
    s.run(5 * MIN);
    s.cam -= 3.0 * HOUR;
    int64_t error = s.frame();
    RTF_ASSERT(s.d.jumped());
    RTF_ASSERT(s.d.jumps() == 1);
    RTF_ASSERT(llabs(error) <= 1);
    RTF_ASSERT(s.d.correction_ms() > 3 * HOUR - SEC);

    s.frame();
    RTF_ASSERT(!s.d.jumped());
    error = s.run(10 * MIN);
    RTF_ASSERT(llabs(error) < 50);
    RTF_ASSERT(s.worst_error < 50);
    RTF_ASSERT(!s.went_back);
    RTF_ASSERT(s.d.jumps() == 1);
}

// Set forward an hour: without the re-anchor the recording would sit an hour
// in the future, where no query looks.
void test_r_ts_discipline::test_clock_jumps_forward()
{
    stream s;
    s.run(5 * MIN);
    int64_t stored_before = s.last_stored;
    s.cam += 1.0 * HOUR;
    int64_t error = s.frame();
    RTF_ASSERT(s.d.jumped());
    RTF_ASSERT(llabs(error) <= 1);
    RTF_ASSERT(s.last_stored > stored_before);

    error = s.run(10 * MIN);
    RTF_ASSERT(llabs(error) < 50);
    RTF_ASSERT(s.worst_error < 50);
    RTF_ASSERT(!s.went_back);
    RTF_ASSERT(s.d.jumps() == 1);
}

// Audio is stored with the correction the last video frame got.
void test_r_ts_discipline::test_audio_correction()
{
    D d;
    RTF_ASSERT(d.correction_ms() == 0);

    stream s;
    s.clock_rate = 1.0 + 2000e-6;
    for(int i = 0; i < 30 * 60 * 10; ++i)
    {
        s.frame();
        RTF_ASSERT(s.last_stored - s.last_raw == s.d.correction_ms());
    }
    RTF_ASSERT(s.d.correction_ms() < -500);
}

namespace
{

// Frames that reach us `late_ms` behind live: ramped in over `ramp_ms`, held
// for `hold_ms`, then drained over `drain_ms`. Returns the furthest any frame
// was stored from when it was captured.
int64_t late_frames(stream& s, double late_ms, int64_t ramp_ms, int64_t hold_ms, int64_t drain_ms)
{
    int64_t worst = 0;
    double start = s.wall;
    double held = start + (double)ramp_ms;
    double draining = held + (double)hold_ms;
    double end = draining + (double)drain_ms;
    while(s.wall < end)
    {
        if(s.wall < held)
            s.delay_ms = late_ms * (s.wall - start) / (double)ramp_ms;
        else if(s.wall < draining)
            s.delay_ms = late_ms;
        else
            s.delay_ms = late_ms * (end - s.wall) / (double)drain_ms;
        s.frame(false);
        worst = (std::max)(worst, (int64_t)llabs(s.last_capture_error));
    }
    s.delay_ms = 0;
    return worst;
}

}

// Frames 5 s late for a minute, with nothing saying they're backed up. The
// camera's clock is fine, so the timeline should stay at capture time; it
// may move later by 60 ms a minute at most, and comes back after.
void test_r_ts_discipline::test_late_frames_capped()
{
    stream s;
    s.watch_lag = false;
    s.run(5 * MIN);
    int64_t worst = late_frames(s, 5.0 * SEC, 2 * SEC, 1 * MIN, 5 * SEC);
    // 68 s of late frames at 60 ms a minute, and a frame of rounding.
    RTF_ASSERT(worst <= 70);
    RTF_ASSERT(worst > 20);
    RTF_ASSERT(s.d.jumps() == 0);
    RTF_ASSERT(!s.went_back);

    s.run(2 * MIN);
    RTF_ASSERT(llabs(s.last_capture_error) <= 10);
    RTF_ASSERT(!s.went_back);

    // Twenty minutes 20 s behind: 1.2 s at most, out again within minutes.
    stream t;
    t.watch_lag = false;
    t.run(5 * MIN);
    worst = late_frames(t, 20.0 * SEC, 1 * MIN, 20 * MIN, 30 * SEC);
    RTF_ASSERT(worst <= 1300);
    t.run(10 * MIN);
    RTF_ASSERT(llabs(t.last_capture_error) <= 10);
    RTF_ASSERT(!t.went_back);
}

// The same, with the lag watchdog saying frames are backed up, as the recorder
// does: the timeline doesn't follow them later at all.
void test_r_ts_discipline::test_late_frames_while_backed_up()
{
    stream s;
    s.run(5 * MIN);
    int64_t worst = late_frames(s, 5.0 * SEC, 2 * SEC, 1 * MIN, 5 * SEC);
    RTF_ASSERT(worst <= 10);

    stream t;
    t.run(5 * MIN);
    worst = late_frames(t, 20.0 * SEC, 1 * MIN, 20 * MIN, 30 * SEC);
    RTF_ASSERT(worst <= 10);
    t.run(5 * MIN);
    RTF_ASSERT(llabs(t.last_capture_error) <= 10);
    RTF_ASSERT(!t.went_back);
    RTF_ASSERT(t.d.jumps() == 0);
}

// Backed up doesn't stop the timeline moving earlier: stored time ahead of
// arrival can only be the camera's clock, however late the frames are.
void test_r_ts_discipline::test_earlier_while_backed_up()
{
    D d;
    const int64_t t0 = 1795000000000;
    // Each frame's stored time is 1 s ahead of its arrival.
    for(int64_t ms = 0; ms <= 60 * SEC; ms += 33)
        d.video_frame(t0 + ms + SEC, t0 + ms, true);
    // A minute at 0.5%: 300 ms taken out.
    RTF_ASSERT(d.correction_ms() <= -250);

    // And the other way, frames 1 s behind: not moved later at all.
    D e;
    for(int64_t ms = 0; ms <= 60 * SEC; ms += 33)
        e.video_frame(t0 + ms - SEC, t0 + ms, true);
    RTF_ASSERT(e.correction_ms() == 0);
}

// How the window carries the backed-up flag, and a long window's slew, on
// frames that are all 1 s late (stored time 1 s behind arrival).
void test_r_ts_discipline::test_backed_up_window()
{
    const int64_t t0 = 1795000000000;
    const int64_t frame = 33;

    // Any backed-up frame holds its window: here only the first frame of each
    // window is flagged (the watchdog's view can change inside a window).
    D d;
    int64_t ms = 0;
    for(; ms <= 60 * SEC; ms += frame)
        d.video_frame(t0 + ms - SEC, t0 + ms, ms % D::WINDOW_MS < frame);
    RTF_ASSERT(d.correction_ms() == 0);

    // Once nothing is flagged, it follows again: a minute at 0.1%, 60 ms.
    for(int64_t end = ms + 60 * SEC; ms <= end; ms += frame)
        d.video_frame(t0 + ms - SEC, t0 + ms, false);
    RTF_ASSERT(d.correction_ms() >= 50);
    RTF_ASSERT(d.correction_ms() <= 62);

    // A window stretched by 20 s with no frames earns no more than two.
    int64_t before = d.correction_ms();
    ms += 20 * SEC;
    d.video_frame(t0 + ms - SEC, t0 + ms, false);
    RTF_ASSERT(d.correction_ms() - before <= 5);
}

// The drift is how far the camera's clock has moved from ours since the
// stream started: 400 ppm fast is 1.44 s an hour, reported negative.
void test_r_ts_discipline::test_drift_reported()
{
    stream s;
    s.clock_rate = 1.0 + 400e-6;
    RTF_ASSERT(s.d.anchored_at_ms() == 0);
    s.run(10 * HOUR);
    RTF_ASSERT(s.d.anchored_at_ms() == s.first_arrival);
    RTF_ASSERT(llabs(s.d.drift_ms() + 14400) < 200);
    RTF_ASSERT(s.d.drift_ms() == s.d.correction_ms());   // no jump: the correction is all drift

    stream t;
    t.clock_rate = 1.0 - 400e-6;
    t.run(10 * HOUR);
    RTF_ASSERT(llabs(t.d.drift_ms() - 14400) < 200);

    stream g;
    g.run(10 * HOUR);
    RTF_ASSERT(llabs(g.d.drift_ms()) < 100);
}

// One warning as the drift reaches each 10 s, either way: three days at
// 400 ppm is about 104 s, ten warnings, never two for the same 10 s.
void test_r_ts_discipline::test_drift_warnings()
{
    stream s;
    s.clock_rate = 1.0 + 400e-6;
    s.run(72 * HOUR);
    RTF_ASSERT(s.drift_warnings.size() == 10);
    for(size_t i = 0; i < s.drift_warnings.size(); ++i)
    {
        int64_t at = -s.drift_warnings[i];
        RTF_ASSERT(at >= (int64_t)(i + 1) * D::DRIFT_WARN_MS && at < (int64_t)(i + 1) * D::DRIFT_WARN_MS + 100);
    }

    stream t;
    t.clock_rate = 1.0 - 400e-6;
    t.run(8 * HOUR);
    RTF_ASSERT(t.drift_warnings.size() == 1);
    RTF_ASSERT(t.drift_warnings[0] >= D::DRIFT_WARN_MS);

    // A good clock: none.
    stream g;
    g.jitter_ms = 40;
    g.run(24 * HOUR);
    RTF_ASSERT(g.drift_warnings.empty());
}

// A jump re-anchors: what it was set to isn't drift, the drift starts again
// from there, and the warnings with it.
void test_r_ts_discipline::test_drift_after_jump()
{
    stream s;
    s.clock_rate = 1.0 + 400e-6;
    s.run(8 * HOUR);
    RTF_ASSERT(s.drift_warnings.size() == 1);
    s.cam -= 3.0 * HOUR;
    s.frame();
    RTF_ASSERT(s.d.jumped());
    RTF_ASSERT(!s.d.drift_warning());
    RTF_ASSERT(s.d.anchored_at_ms() == s.last_arrival);
    RTF_ASSERT(llabs(s.d.drift_ms()) <= 1);
    RTF_ASSERT(s.d.correction_ms() > 3 * HOUR - 20 * SEC);

    s.run(8 * HOUR);
    RTF_ASSERT(llabs(s.d.drift_ms() + 11520) < 200);
    RTF_ASSERT(s.drift_warnings.size() == 2);
    RTF_ASSERT(-s.drift_warnings[1] >= D::DRIFT_WARN_MS && -s.drift_warnings[1] < D::DRIFT_WARN_MS + 100);
}

void test_r_ts_discipline::test_drift_message()
{
    RTF_ASSERT(drift_message(-10000, (int64_t)(8.3 * HOUR)) ==
               "the camera's clock is 10.0 s fast of ours after 8.3 h (about 1.2 s an hour); "
               "the recording is kept on our clock; check that the camera takes its time from the internet (NTP)");
    RTF_ASSERT(drift_message(20500, 10 * HOUR) ==
               "the camera's clock is 20.5 s slow of ours after 10.0 h (about 2.0 s an hour); "
               "the recording is kept on our clock; check that the camera takes its time from the internet (NTP)");
    // Too soon for a rate.
    RTF_ASSERT(drift_message(-10000, 2 * MIN) ==
               "the camera's clock is 10.0 s fast of ours; "
               "the recording is kept on our clock; check that the camera takes its time from the internet (NTP)");
}

void test_r_ts_discipline::test_recording_clock_json()
{
    auto j = recording_clock_json(true, -90000, 1795000000000, 2);
    RTF_ASSERT(j.is_object());
    RTF_ASSERT(j["drift_ms"].get<int64_t>() == -90000);
    RTF_ASSERT(j["since_ms"].get<int64_t>() == 1795000000000);
    RTF_ASSERT(j["jumps"].get<int>() == 2);
    RTF_ASSERT(recording_clock_json(false, -90000, 1795000000000, 2).is_null());
    RTF_ASSERT(recording_clock_json(true, 0, 0, 0).is_null());
}
