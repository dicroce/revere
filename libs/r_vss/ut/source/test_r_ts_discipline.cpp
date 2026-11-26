
#include "test_r_ts_discipline.h"
#include "r_vss/r_ts_discipline.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

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
// does, the first frame's time is the wall clock when it arrived.
struct stream
{
    D d;
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
        int64_t stored = raw + d.video_frame(raw, arrival);

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

    // A bad one: 0.2% slow, 7 s an hour.
    stream t;
    t.clock_rate = 1.0 - 2000e-6;
    t.run(24 * HOUR);
    RTF_ASSERT(t.worst_error < 150);
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

// A Wi-Fi stall: nothing for 8 s, then the backlog arrives at once. The
// frames are late, not the camera's clock wrong, so the timeline barely moves
// and is back once the stream is live again.
void test_r_ts_discipline::test_stall_then_backlog()
{
    stream s;
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
    RTF_ASSERT(most_moved <= 40);
    RTF_ASSERT(s.d.jumps() == 0);
    RTF_ASSERT(!s.went_back);

    int64_t error = s.run(3 * MIN, false);
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
