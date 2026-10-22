
#include "test_r_lag_watchdog.h"
#include "r_vss/r_lag_watchdog.h"
#include <cmath>
#include <cstdint>

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_lag_watchdog);

typedef r_lag_watchdog W;

static const int64_t SEC = 1000;
static const int64_t MIN = 60 * SEC;

namespace
{

// A simulated camera stream. Camera time advances one frame interval per
// frame; arrival advances the same divided by `rate` (1.0 live, under 1.0 a
// growing backlog, over 1.0 a backlog draining), plus up to `jitter_ms` of
// network jitter that doesn't accumulate.
struct stream
{
    W w;
    double cam {0};
    double base {1000000};
    double interval {1000.0 / 30};
    int64_t jitter_ms {0};
    uint32_t rng {12345};

    int64_t first_lagging {-1};
    int64_t first_reconnect {-1};
    int started {0};
    int caught_up {0};
    int jumps {0};

    int64_t now() const { return (int64_t)base; }

    // cam_step: camera time since the last frame (default one interval);
    // anything else is a camera clock jump.
    W::r_lag_event frame(double rate = 1.0, double cam_step = NAN)
    {
        cam += std::isnan(cam_step) ? interval : cam_step;
        base += interval / rate;
        rng = rng * 1103515245 + 12345;
        int64_t j = (jitter_ms > 0) ? (int64_t)((rng >> 8) % (uint32_t)jitter_ms) : 0;
        auto e = w.frame((int64_t)llround(cam), (int64_t)llround(base) + j);
        if(e == W::lag_started) ++started;
        if(e == W::lag_caught_up) ++caught_up;
        if(e == W::lag_clock_jump) ++jumps;
        if(w.lagging() && first_lagging < 0) first_lagging = now();
        if(w.wants_reconnect() && first_reconnect < 0) first_reconnect = now();
        return e;
    }

    // Frames for `ms` of arrival time.
    void run(int64_t ms, double rate = 1.0)
    {
        auto end = base + ms;
        while(base < end)
            frame(rate);
    }

    // Nothing arrives for `ms` (a stall); the camera keeps capturing, and its
    // frames come later, queued.
    void stall(int64_t ms) { base += ms; }

    // A queued backlog drains at `rate` until lag is back under `until_ms`.
    void drain(double rate, int64_t until_ms = 50)
    {
        int i = 0;
        do
            frame(rate);
        while(++i < 100000 && w.lag_ms() > until_ms);
    }
};

}

void test_r_lag_watchdog::test_live_stream_with_jitter()
{
    // 30 minutes live, up to 80 ms of jitter on every frame: never behind.
    stream s;
    s.jitter_ms = 80;
    int64_t worst = 0;
    auto end = s.base + 30 * MIN;
    while(s.base < end)
    {
        s.frame();
        worst = max(worst, s.w.lag_ms());
    }
    RTF_ASSERT(worst < 100);
    RTF_ASSERT(worst > 0);
    RTF_ASSERT_EQUAL(s.first_lagging, (int64_t)-1);
    RTF_ASSERT_EQUAL(s.first_reconnect, (int64_t)-1);
    RTF_ASSERT_EQUAL(s.w.episodes(), 0);
}

void test_r_lag_watchdog::test_slow_camera_crystal()
{
    // A crystal 100 ppm slow, for 2 hours: 0.72 s adrift by the end.
    {
        stream s;
        s.run(2 * 60 * MIN, 1.0 - 100e-6);
        RTF_ASSERT(s.w.lag_ms() < 50);
        RTF_ASSERT_EQUAL(s.first_lagging, (int64_t)-1);
    }
    // The worst a clock should do: an NTP slew (500 ppm) on a crystal 100 ppm
    // slow, for 2 hours: 4.3 s adrift by the end.
    {
        stream s;
        s.run(2 * 60 * MIN, 1.0 - 600e-6);
        RTF_ASSERT(s.w.lag_ms() < 50);
        RTF_ASSERT_EQUAL(s.first_lagging, (int64_t)-1);
        RTF_ASSERT_EQUAL(s.first_reconnect, (int64_t)-1);
    }
    // But a link 0.4% short is caught within 5 minutes (~3 ms a second
    // over the allowance), long before it is 7 s behind.
    {
        stream s;
        s.run(MIN);
        auto t0 = s.now();
        s.run(30 * MIN, 0.996);
        RTF_ASSERT(s.first_lagging > 0);
        RTF_ASSERT(s.first_lagging - t0 < 5 * MIN);
    }
    // And a link 2% short within a minute.
    {
        stream s;
        s.run(MIN);
        auto t0 = s.now();
        s.run(2 * MIN, 0.98);
        RTF_ASSERT(s.first_lagging > 0);
        RTF_ASSERT(s.first_lagging - t0 < MIN);
    }
}

void test_r_lag_watchdog::test_backlog_lags_then_reconnects()
{
    // Live for a minute, then camera time at 0.8x arrival time: lag grows
    // ~199 ms a second (200 less the drift allowance). Over 500 ms at ~2.5 s,
    // lagging 2 s later; over 2 s at ~10.3 s, reconnect 1 s later.
    stream s;
    s.run(MIN);
    RTF_ASSERT_EQUAL(s.first_lagging, (int64_t)-1);
    auto t0 = s.now();
    s.run(20 * SEC, 0.8);

    RTF_ASSERT(s.first_lagging - t0 >= 4300);
    RTF_ASSERT(s.first_lagging - t0 <= 4900);
    RTF_ASSERT_EQUAL(s.started, 1);
    RTF_ASSERT(s.first_reconnect - t0 >= 11000);
    RTF_ASSERT(s.first_reconnect - t0 <= 11600);
    RTF_ASSERT(s.w.lag_ms() > 3500);
    RTF_ASSERT(s.w.lagging());
    RTF_ASSERT(s.w.wants_reconnect());
    RTF_ASSERT_EQUAL(s.w.episodes(), 1);
}

void test_r_lag_watchdog::test_stuck_behind_reconnects()
{
    // An 800 ms stall, then frames at the live rate but 800 ms late: behind
    // and not draining. Lagging after 2 s; never over 2 s, so the reconnect
    // comes from STUCK_MS, at 10 s and not before.
    stream s;
    s.run(MIN);
    s.stall(800);
    auto t0 = s.now();
    s.run(9 * SEC);
    RTF_ASSERT(s.first_lagging - t0 >= 2000);
    RTF_ASSERT(s.first_lagging - t0 <= 2100);
    RTF_ASSERT_EQUAL(s.first_reconnect, (int64_t)-1);
    RTF_ASSERT(!s.w.wants_reconnect());
    s.run(2 * SEC);
    RTF_ASSERT(s.first_reconnect - t0 >= 10000);
    RTF_ASSERT(s.first_reconnect - t0 <= 10100);
    RTF_ASSERT(s.w.lag_ms() < W::RECONNECT_LAG_MS);
}

void test_r_lag_watchdog::test_blips_that_drain()
{
    // 1.5 s stalls draining at 3x, every 30 s for 10 minutes: over 500 ms for
    // ~1.5 s each, under LAG_SUSTAIN_MS. Nothing logged, no reconnect.
    {
        stream s;
        s.run(MIN);
        for(int i = 0; i < 20; ++i)
        {
            s.stall(1500);
            s.drain(3.0);
            s.run(30 * SEC);
        }
        RTF_ASSERT_EQUAL(s.first_lagging, (int64_t)-1);
        RTF_ASSERT_EQUAL(s.first_reconnect, (int64_t)-1);
        RTF_ASSERT_EQUAL(s.started, 0);
    }
    // A 2.8 s stall draining at 2x (lag falls 1 ms a ms): over 2 s for
    // ~0.8 s, under RECONNECT_SUSTAIN_MS, so no reconnect; over 500 ms for
    // ~2.3 s, so it's logged as an episode.
    {
        stream s;
        s.run(MIN);
        s.stall(2800);
        s.drain(2.0);
        s.run(30 * SEC);
        RTF_ASSERT_EQUAL(s.first_reconnect, (int64_t)-1);
        RTF_ASSERT_EQUAL(s.started, 1);
        RTF_ASSERT_EQUAL(s.caught_up, 1);
        RTF_ASSERT(!s.w.lagging());
    }
    // A 3.5 s stall draining at 2x: over 2 s for ~1.5 s, but falling all the
    // while. Left to drain (reconnecting would lose the queued footage).
    {
        stream s;
        s.run(MIN);
        s.stall(3500);
        s.drain(2.0);
        s.run(30 * SEC);
        RTF_ASSERT_EQUAL(s.first_reconnect, (int64_t)-1);
        RTF_ASSERT_EQUAL(s.caught_up, 1);
    }
    // A 6 s stall draining at 1.5x: falling, so not the fast reconnect, but
    // over 500 ms for ~11 s: STUCK_MS is the backstop.
    {
        stream s;
        s.run(MIN);
        s.stall(6000);
        auto t0 = s.now();
        s.drain(1.5);
        RTF_ASSERT(s.first_reconnect - t0 >= 10000);
        RTF_ASSERT(s.first_reconnect - t0 <= 10100);
    }
}

void test_r_lag_watchdog::test_stuck_over_2s_reconnects()
{
    // A 2.5 s stall, then frames at the live rate: over 2 s and not falling.
    // The fast reconnect, 1 s after.
    {
        stream s;
        s.run(MIN);
        s.stall(2500);
        auto t0 = s.now();
        s.run(5 * SEC);
        RTF_ASSERT(s.first_reconnect - t0 >= 1000);
        RTF_ASSERT(s.first_reconnect - t0 <= 1100);
    }
    // Draining, then stuck again before it gets under 2 s: once it stops
    // falling from its peak it waits for STUCK_MS rather than reconnecting
    // straight away, as it is still under the peak.
    {
        stream s;
        s.run(MIN);
        s.stall(4000);
        auto t0 = s.now();
        s.drain(2.0, 3000);
        s.run(8 * SEC);
        RTF_ASSERT_EQUAL(s.first_reconnect, (int64_t)-1);
        s.run(3 * SEC);
        RTF_ASSERT(s.first_reconnect - t0 >= 10000);
        RTF_ASSERT(s.first_reconnect - t0 <= 10100);
    }
}

void test_r_lag_watchdog::test_camera_clock_jumps()
{
    stream s;
    s.jitter_ms = 40;
    s.run(MIN);

    // Forward an hour: absorbed.
    s.frame(1.0, 60 * MIN);
    s.run(MIN);
    RTF_ASSERT(s.w.lag_ms() < 60);
    RTF_ASSERT_EQUAL(s.jumps, 0);

    // Back two hours: re-anchored.
    RTF_ASSERT_EQUAL((int)s.frame(1.0, -2 * 60 * MIN), (int)W::lag_clock_jump);
    RTF_ASSERT_EQUAL(s.w.lag_ms(), (int64_t)0);
    s.run(MIN);
    RTF_ASSERT(s.w.lag_ms() < 60);

    // Back 300 ms (over JUMP_MS): re-anchored. Back 200 ms (B-frame range):
    // shows as 200 ms of lag that the drift allowance wears away, never 500.
    s.frame(1.0, -300);
    RTF_ASSERT_EQUAL(s.jumps, 2);
    s.run(MIN);
    s.frame(1.0, -200);
    RTF_ASSERT_EQUAL(s.jumps, 2);
    RTF_ASSERT(s.w.lag_ms() >= 200);
    s.run(5 * MIN);
    RTF_ASSERT(s.w.lag_ms() < 60);

    // Jumping back and forth by 5 s every 10 s for 10 minutes.
    for(int i = 0; i < 60; ++i)
    {
        s.frame(1.0, (i % 2) ? -5 * SEC : 5 * SEC);
        s.run(10 * SEC);
    }

    RTF_ASSERT_EQUAL(s.first_lagging, (int64_t)-1);
    RTF_ASSERT_EQUAL(s.first_reconnect, (int64_t)-1);
    RTF_ASSERT_EQUAL(s.w.episodes(), 0);
    RTF_ASSERT_EQUAL(s.w.clock_jumps(), 32);

    // A real backlog right after a jump is still caught.
    s.frame(1.0, -10 * SEC);
    auto t0 = s.now();
    s.run(10 * SEC, 0.8);
    RTF_ASSERT(s.first_lagging > 0);
    RTF_ASSERT(s.first_lagging - t0 <= 4900);
}

void test_r_lag_watchdog::test_b_frame_order()
{
    // Decode order I0 P3 B1 B2 P6 B4 B5 ...: pts step back up to two frames.
    W w;
    double interval = 1000.0 / 30;
    int64_t arrival = 1000000;
    int64_t worst = 0;
    w.frame(0, arrival);
    for(int g = 0; g < 30 * 60 * 10 / 3; ++g)
    {
        for(int idx : {3 * g + 3, 3 * g + 1, 3 * g + 2})
        {
            arrival += 33;
            w.frame((int64_t)llround(idx * interval), arrival);
            worst = max(worst, w.lag_ms());
        }
    }
    RTF_ASSERT_EQUAL(w.clock_jumps(), 0);
    RTF_ASSERT(!w.lagging());
    RTF_ASSERT_EQUAL(w.episodes(), 0);
    RTF_ASSERT(worst < 150);
}

void test_r_lag_watchdog::test_connect_burst()
{
    // At connect, a GOP (1 s of camera time) arrives in 60 ms, then live.
    W w;
    int64_t arrival = 1000000;
    for(int i = 0; i < 30; ++i)
        w.frame(i * 33, arrival + i * 2);
    int64_t cam = 29 * 33;
    arrival += 60;
    for(int i = 0; i < 30 * 60; ++i)
    {
        cam += 33;
        arrival += 33;
        w.frame(cam, arrival);
    }
    RTF_ASSERT(w.lag_ms() < 50);
    RTF_ASSERT_EQUAL(w.episodes(), 0);
    RTF_ASSERT(!w.wants_reconnect());
}

void test_r_lag_watchdog::test_caught_up()
{
    stream s;
    s.run(MIN);
    s.stall(2800);
    auto t0 = s.now();
    while(s.caught_up == 0 && s.now() - t0 < 10 * SEC)
        s.frame(2.0);
    RTF_ASSERT_EQUAL(s.caught_up, 1);
    RTF_ASSERT(s.w.peak_lag_ms() >= 2700);
    RTF_ASSERT(s.w.last_episode_ms() >= 2000);
    RTF_ASSERT(s.w.last_episode_ms() <= 3000);
    RTF_ASSERT(s.w.lag_ms() < W::CAUGHT_UP_MS);
    RTF_ASSERT(!s.w.lagging());
    RTF_ASSERT_EQUAL(s.w.behind_for_ms(s.now()), (int64_t)0);

    // Under LAG_MS but not yet under CAUGHT_UP_MS: the episode carries on.
    stream d;
    d.run(MIN);
    d.stall(3000);
    d.drain(1.5, 400);
    RTF_ASSERT(d.w.lag_ms() > W::CAUGHT_UP_MS);
    RTF_ASSERT(d.w.lagging());
    RTF_ASSERT_EQUAL(d.caught_up, 0);
}

void test_r_lag_watchdog::test_rate()
{
    stream s;
    RTF_ASSERT(s.w.rate() == 1.0);
    s.run(MIN);
    RTF_ASSERT(fabs(s.w.rate() - 1.0) < 0.01);
    s.run(10 * SEC, 0.8);
    RTF_ASSERT(fabs(s.w.rate() - 0.8) < 0.02);
    auto snap = s.w.snapshot(s.now());
    RTF_ASSERT(fabs(snap.rate - 0.8) < 0.02);
    RTF_ASSERT_EQUAL(snap.lag_ms, s.w.lag_ms());
    RTF_ASSERT(snap.lagging);
    RTF_ASSERT(snap.behind_for_ms > 6000);
    RTF_ASSERT_EQUAL(snap.episodes, 1);
}

void test_r_lag_watchdog::test_reconnect_backoff()
{
    r_lag_reconnects r;
    int64_t t = 10 * MIN;

    // The first is immediate; then 30 s, 60 s, 120 s ... between them.
    RTF_ASSERT(r.try_reconnect("a", t));
    RTF_ASSERT_EQUAL(r.held_off_ms("a", t), (int64_t)30 * SEC);
    RTF_ASSERT(!r.try_reconnect("a", t + 29 * SEC));
    RTF_ASSERT(r.try_reconnect("a", t + 30 * SEC));
    t += 30 * SEC;
    RTF_ASSERT_EQUAL(r.held_off_ms("a", t), (int64_t)60 * SEC);
    RTF_ASSERT(!r.try_reconnect("a", t + 59 * SEC));
    RTF_ASSERT(r.try_reconnect("a", t + 60 * SEC));
    t += 60 * SEC;
    RTF_ASSERT_EQUAL(r.held_off_ms("a", t), (int64_t)120 * SEC);

    // Doubling stops at MAX_HOLD_MS.
    for(int i = 0; i < 10; ++i)
    {
        t += r.held_off_ms("a", t);
        RTF_ASSERT(r.try_reconnect("a", t));
    }
    RTF_ASSERT_EQUAL(r.held_off_ms("a", t), (int64_t)r_lag_reconnects::MAX_HOLD_MS);

    // Another camera isn't held off.
    RTF_ASSERT(r.try_reconnect("b", t));
    RTF_ASSERT_EQUAL(r.held_off_ms("c", t), (int64_t)0);

    // RESET_MS without one, and the wait starts again from 30 s.
    t += r_lag_reconnects::RESET_MS + 1;
    RTF_ASSERT(r.try_reconnect("a", t));
    RTF_ASSERT_EQUAL(r.held_off_ms("a", t), (int64_t)30 * SEC);
}

void test_r_lag_watchdog::test_reconnect_flag()
{
    r_lag_reconnects r;
    int64_t t = 10 * MIN;
    RTF_ASSERT(!r.flagged("a", t));
    RTF_ASSERT(r.try_reconnect("a", t));
    RTF_ASSERT_EQUAL(r.recent("a", t), 1);
    RTF_ASSERT(!r.flagged("a", t));
    RTF_ASSERT(r.try_reconnect("a", t + 40 * MIN));
    RTF_ASSERT_EQUAL(r.recent("a", t + 40 * MIN), 2);
    RTF_ASSERT(r.flagged("a", t + 40 * MIN));
    RTF_ASSERT(!r.flagged("b", t + 40 * MIN));
    // An hour after the first, one is left in the window: no longer flagged.
    RTF_ASSERT(r.flagged("a", t + 60 * MIN));
    RTF_ASSERT(!r.flagged("a", t + 61 * MIN));
    RTF_ASSERT_EQUAL(r.recent("a", t + 101 * MIN), 0);
}

void test_r_lag_watchdog::test_newest_frame_gate()
{
    r_newest_frame_gate g;
    auto a1 = g.posted("a");
    auto a2 = g.posted("a");
    auto b1 = g.posted("b");

    // A newer key frame from the same camera is queued: skip once stale.
    RTF_ASSERT(g.skip("a", a1, r_newest_frame_gate::STALE_MS + 1));
    RTF_ASSERT(!g.skip("a", a1, r_newest_frame_gate::STALE_MS));
    // The newest is never skipped, however long it waited.
    RTF_ASSERT(!g.skip("a", a2, 60 * SEC));
    // Another camera's newer frame doesn't count.
    RTF_ASSERT(!g.skip("b", b1, 60 * SEC));
    RTF_ASSERT(!g.skip("unknown", 1, 60 * SEC));

    g.forget("a");
    RTF_ASSERT(!g.skip("a", a1, 60 * SEC));
    RTF_ASSERT_EQUAL(g.posted("a"), (uint64_t)1);
}
