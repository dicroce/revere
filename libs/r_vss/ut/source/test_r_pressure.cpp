#include "test_r_pressure.h"
#include "r_vss/r_pressure.h"
#include "r_vss/r_cpu_sampler.h"
#include "r_storage/r_write_delay.h"
#include "r_utils/3rdparty/json/json.h"
#include <string>

using namespace r_vss;
using namespace std;
using json = nlohmann::json;

REGISTER_TEST_FIXTURE(test_r_pressure);

namespace
{

const signal_state U = signal_state::unknown, OK = signal_state::ok, EL = signal_state::elevated, HI = signal_state::high;

signal_state state_of(const r_pressure_result& r, const string& id)
{
    for(const auto& s : r.signals)
        if(s.id == id)
            return s.state;
    return signal_state::unknown;
}

bool has_reason(const r_pressure_result& r, const string& id)
{
    for(const auto& s : r.signals)
        if(s.id == id && !s.text.empty())
            return true;
    return false;
}

// A CPU reading that is `busy` of `total` more than the one before.
r_cpu_ticks more(const r_cpu_ticks& was, uint64_t busy, uint64_t total)
{
    r_cpu_ticks t;
    t.busy = was.busy + busy;
    t.total = was.total + total;
    return t;
}

// Counters of a made-up machine at a fixed busy fraction, read every 2 s from `from_ms` for `for_ms`.
struct fake_cpu
{
    r_cpu_ticks ticks;
    int64_t at {0};
    double busy {0.0};

    void run(r_pressure_monitor& m, int64_t for_ms)
    {
        const int64_t end = at + for_ms;
        while(at < end)
        {
            at += 2000;
            ticks = more(ticks, (uint64_t)(busy * 1000), 1000);
            m.tick(at);
        }
    }
};

r_pressure_sources cpu_only(fake_cpu& c)
{
    r_pressure_sources s;
    s.cpu_ticks = [&c](r_cpu_ticks& t) { t = c.ticks; return true; };
    return s;
}

}

void test_r_pressure::test_nothing_known_is_ok_and_lists_every_signal_unknown()
{
    auto r = evaluate_pressure(r_pressure_inputs());
    RTF_ASSERT(r.level == pressure_level::ok);
    RTF_ASSERT(r.signals.size() == 6);
    for(const auto& s : r.signals)
        RTF_ASSERT(s.state == U && s.text.empty());
}

void test_r_pressure::test_motion_signal()
{
    r_pressure_inputs in;
    in.motion_known = true;
    RTF_ASSERT(state_of(evaluate_pressure(in), "motion_drop") == OK);
    // A queue of 250 is not over; 251 is.
    in.motion_queue = 250;
    RTF_ASSERT(state_of(evaluate_pressure(in), "motion_drop") == OK);
    in.motion_queue = 251;
    RTF_ASSERT(state_of(evaluate_pressure(in), "motion_drop") == EL);
    in.motion_queue = 0;
    // A skip in the last 30 s: elevated; 30 s and a bit ago: not; never: not.
    in.motion_skip_age_ms = 30000;
    RTF_ASSERT(state_of(evaluate_pressure(in), "motion_drop") == EL);
    in.motion_skip_age_ms = 30001;
    RTF_ASSERT(state_of(evaluate_pressure(in), "motion_drop") == OK);
    in.motion_skip_age_ms = -1;
    // Any frame dropped lately: high, with its own words.
    in.motion_drop_age_ms = 0;
    auto r = evaluate_pressure(in);
    RTF_ASSERT(state_of(r, "motion_drop") == HI && r.level == pressure_level::high);
    for(const auto& s : r.signals)
        if(s.id == "motion_drop")
            RTF_ASSERT(s.text == "motion detection is dropping frames");
    in.motion_drop_age_ms = 30001;
    RTF_ASSERT(state_of(evaluate_pressure(in), "motion_drop") == OK);
}

void test_r_pressure::test_detection_signal()
{
    r_pressure_inputs in;
    in.detection_known = true;
    in.detection_baseline_ms = 200.0;
    in.detection_p90_ms = 300.0;      // exactly 1.5 times: not over
    RTF_ASSERT(state_of(evaluate_pressure(in), "detection_slow") == OK);
    in.detection_p90_ms = 301.0;
    RTF_ASSERT(state_of(evaluate_pressure(in), "detection_slow") == EL);
    in.detection_p90_ms = 500.0;      // exactly 2.5 times: not over
    RTF_ASSERT(state_of(evaluate_pressure(in), "detection_slow") == EL);
    in.detection_p90_ms = 501.0;
    RTF_ASSERT(state_of(evaluate_pressure(in), "detection_slow") == HI);
    // Over 2 s is high whatever the baseline, and with no baseline yet only that counts.
    in.detection_baseline_ms = 0.0;
    in.detection_p90_ms = 1900.0;
    RTF_ASSERT(state_of(evaluate_pressure(in), "detection_slow") == OK);
    in.detection_p90_ms = 2001.0;
    RTF_ASSERT(state_of(evaluate_pressure(in), "detection_slow") == HI);
    in.detection_baseline_ms = 1000.0;
    in.detection_p90_ms = 2000.0;
    RTF_ASSERT(state_of(evaluate_pressure(in), "detection_slow") == EL);   // 2 times, and exactly 2 s: not over
}

void test_r_pressure::test_write_signal()
{
    r_pressure_inputs in;
    in.writes_known = true;
    in.write_delay_ms = 1000;
    RTF_ASSERT(state_of(evaluate_pressure(in), "write_delay") == OK);
    in.write_delay_ms = 1001;
    RTF_ASSERT(state_of(evaluate_pressure(in), "write_delay") == EL);
    in.write_delay_ms = 3000;
    RTF_ASSERT(state_of(evaluate_pressure(in), "write_delay") == EL);
    in.write_delay_ms = 3001;
    RTF_ASSERT(state_of(evaluate_pressure(in), "write_delay") == HI);
    // Growing for 10 s is high once it is over the elevated line, and not before (a few ms drifting up is no backlog).
    in.write_delay_ms = 1500;
    in.write_delay_growing = true;
    RTF_ASSERT(state_of(evaluate_pressure(in), "write_delay") == HI);
    in.write_delay_ms = 900;
    RTF_ASSERT(state_of(evaluate_pressure(in), "write_delay") == OK);
}

void test_r_pressure::test_cpu_signal()
{
    r_pressure_inputs in;
    in.cpu_known = true;
    in.cpu_elevated_for_ms = 29999;
    RTF_ASSERT(state_of(evaluate_pressure(in), "cpu_busy") == OK);
    in.cpu_elevated_for_ms = 30000;
    RTF_ASSERT(state_of(evaluate_pressure(in), "cpu_busy") == EL);
    in.cpu_high_for_ms = 9999;
    RTF_ASSERT(state_of(evaluate_pressure(in), "cpu_busy") == EL);
    in.cpu_high_for_ms = 10000;
    RTF_ASSERT(state_of(evaluate_pressure(in), "cpu_busy") == HI);
}

void test_r_pressure::test_cameras_behind_count_only_while_the_cpu_is_busy()
{
    r_pressure_inputs in;
    in.lag_known = true;
    in.lag_camera_flagged = true;
    in.cpu_known = true;
    // A flagged camera with the CPU not busy is a slow network, not revere's: ok.
    RTF_ASSERT(state_of(evaluate_pressure(in), "camera_lag") == OK);
    // Unknown CPU doesn't count either.
    in.cpu_known = false;
    RTF_ASSERT(state_of(evaluate_pressure(in), "camera_lag") == OK);
    in.cpu_known = true;
    in.cpu_elevated_for_ms = 30000;
    RTF_ASSERT(state_of(evaluate_pressure(in), "camera_lag") == EL);
    // No camera flagged: ok however busy.
    in.lag_camera_flagged = false;
    RTF_ASSERT(state_of(evaluate_pressure(in), "camera_lag") == OK);
}

void test_r_pressure::test_hardware_encoder_signal()
{
    r_pressure_inputs in;
    in.hw_known = true;
    in.hw_limit = 3;
    in.hw_used = 1;
    RTF_ASSERT(state_of(evaluate_pressure(in), "hw_encoder") == OK);
    in.hw_used = 2;      // within 1 of the limit
    RTF_ASSERT(state_of(evaluate_pressure(in), "hw_encoder") == EL);
    in.hw_used = 3;
    RTF_ASSERT(state_of(evaluate_pressure(in), "hw_encoder") == HI);
    // No limit (no hardware encoder): unknown, whatever is used.
    in.hw_limit = 0;
    RTF_ASSERT(state_of(evaluate_pressure(in), "hw_encoder") == U);
}

void test_r_pressure::test_level_is_the_worst_and_every_reason_is_listed()
{
    r_pressure_inputs in;
    in.motion_known = true;
    in.motion_queue = 400;                // elevated
    in.writes_known = true;
    in.write_delay_ms = 5000;             // high
    in.cpu_known = true;
    in.cpu_elevated_for_ms = 60000;       // elevated
    in.lag_known = true;
    in.lag_camera_flagged = true;         // elevated, with the CPU
    auto r = evaluate_pressure(in);
    RTF_ASSERT(r.level == pressure_level::high);
    RTF_ASSERT(has_reason(r, "motion_drop") && has_reason(r, "write_delay") && has_reason(r, "cpu_busy") && has_reason(r, "camera_lag"));
    RTF_ASSERT(!has_reason(r, "detection_slow") && !has_reason(r, "hw_encoder"));
    // Without the high one, elevated.
    in.write_delay_ms = 0;
    r = evaluate_pressure(in);
    RTF_ASSERT(r.level == pressure_level::elevated && !has_reason(r, "write_delay"));
}

void test_r_pressure::test_cpu_sampler_average_and_holds()
{
    r_cpu_sampler s;
    r_cpu_ticks t;
    int64_t now = 0;
    s.add(now, t);                          // the starting point
    RTF_ASSERT(!s.has_sample() && s.busy() < 0.0);
    // Busy 80% for 40 s, read every 2 s.
    for(int i = 0; i < 20; ++i)
    {
        now += 2000;
        t = more(t, 800, 1000);
        s.add(now, t);
    }
    RTF_ASSERT(s.has_sample() && s.busy() > 0.79 && s.busy() < 0.81);
    // Over 75% since the first sample: held 38 s by now (the first sample is at 2 s); under 90%.
    RTF_ASSERT(s.elevated_for_ms(now) == 38000);
    RTF_ASSERT(s.high_for_ms(now) == 0);
    // A drop to 10% takes the 10 s average down gradually, then ends the hold.
    for(int i = 0; i < 6; ++i)
    {
        now += 2000;
        t = more(t, 100, 1000);
        s.add(now, t);
    }
    RTF_ASSERT(s.busy() < 0.75 && s.elevated_for_ms(now) == 0);
    // Busy again at 95%: high is held from when the average crosses 90%.
    int64_t crossed = -1;
    for(int i = 0; i < 12; ++i)
    {
        now += 2000;
        t = more(t, 950, 1000);
        s.add(now, t);
        if(crossed < 0 && s.busy() >= 0.90)
            crossed = now;
    }
    RTF_ASSERT(crossed > 0 && s.high_for_ms(now) == now - crossed);
}

void test_r_pressure::test_cpu_sampler_ignores_resets_and_stalls()
{
    r_cpu_sampler s;
    r_cpu_ticks t;
    s.add(0, t);
    // Counters that didn't move: no reading.
    s.add(2000, t);
    RTF_ASSERT(!s.has_sample());
    t = more(t, 500, 1000);
    s.add(4000, t);
    RTF_ASSERT(s.has_sample() && s.busy() > 0.49 && s.busy() < 0.51);
    // Counters that went back (a reset): no reading, and the next one measures from the new start.
    r_cpu_ticks reset;
    reset.busy = 10;
    reset.total = 20;
    s.add(6000, reset);
    RTF_ASSERT(s.busy() > 0.49 && s.busy() < 0.51);
    s.add(8000, more(reset, 1000, 1000));
    RTF_ASSERT(s.busy() > 0.74 && s.busy() < 0.76);      // (0.5 + 1.0) / 2
}

void test_r_pressure::test_cpu_sampler_day_percentile()
{
    RTF_ASSERT(r_cpu_sampler().busy_p95_24h() < 0.0);
    // A reading a minute is enough: each finished minute is one value, and the one being summed isn't counted yet, so
    // 100 readings give 99 values. The 95th percentile is the nearest rank: the 95th of 99.
    auto run = [](int calm_minutes) {
        r_cpu_sampler s;
        r_cpu_ticks t;
        int64_t now = 0;
        s.add(now, t);
        for(int m = 0; m < 100; ++m)
        {
            now += 60000;
            t = more(t, m < calm_minutes ? 200 : 900, 1000);
            s.add(now, t);
        }
        return s.busy_p95_24h();
    };
    // 95 calm minutes then 4 busy among the 99: the 95th is still calm.
    RTF_ASSERT(run(95) > 0.19 && run(95) < 0.21);
    // 90 calm then 9 busy: the 95th is busy.
    RTF_ASSERT(run(90) > 0.89 && run(90) < 0.91);
}

void test_r_pressure::test_monitor_cpu_to_level()
{
    fake_cpu c;
    r_pressure_monitor m(cpu_only(c));
    c.busy = 0.5;
    c.run(m, 20000);
    RTF_ASSERT(m.evaluate(c.at).level == pressure_level::ok);
    // 80% for 20 s is not yet 30 s.
    c.busy = 0.8;
    c.run(m, 20000);
    RTF_ASSERT(m.evaluate(c.at).level == pressure_level::ok);
    c.run(m, 20000);
    RTF_ASSERT(m.evaluate(c.at).level == pressure_level::elevated);
    // 95% for 10 s after the average crosses 90%.
    c.busy = 0.95;
    c.run(m, 30000);
    RTF_ASSERT(m.evaluate(c.at).level == pressure_level::high);
    // Calm again: ok.
    c.busy = 0.1;
    c.run(m, 20000);
    RTF_ASSERT(m.evaluate(c.at).level == pressure_level::ok);
}

void test_r_pressure::test_monitor_motion_notes_and_queue()
{
    size_t queue = 0;
    r_pressure_sources s;
    s.motion_queue_size = [&queue]() { return queue; };
    r_pressure_monitor m(s);
    m.tick(1000);
    RTF_ASSERT(state_of(m.evaluate(1000), "motion_drop") == OK);
    m.note_motion_skip(1000);
    RTF_ASSERT(state_of(m.evaluate(2000), "motion_drop") == EL);
    RTF_ASSERT(state_of(m.evaluate(31001), "motion_drop") == OK);
    m.note_motion_dropped(0, 40000);      // nothing dropped
    RTF_ASSERT(state_of(m.evaluate(40001), "motion_drop") == OK);
    m.note_motion_dropped(3, 40000);
    RTF_ASSERT(state_of(m.evaluate(40001), "motion_drop") == HI);
    queue = 300;
    m.tick(80000);
    RTF_ASSERT(state_of(m.evaluate(80000), "motion_drop") == EL);
    // Without a motion queue source it is unknown, even after notes.
    r_pressure_monitor none{r_pressure_sources()};
    none.note_motion_dropped(3, 1000);
    RTF_ASSERT(state_of(none.evaluate(1000), "motion_drop") == U);
}

void test_r_pressure::test_monitor_write_delay_growing()
{
    int64_t delay = -1;
    r_pressure_sources s;
    s.take_write_delay_max = [&delay]() { return delay; };
    r_pressure_monitor m(s);
    // No writes: the delay is 0 and known.
    m.tick(2000);
    RTF_ASSERT(state_of(m.evaluate(2000), "write_delay") == OK);
    // Steady at 1.5 s: elevated, not growing.
    int64_t now = 2000;
    delay = 1500;
    for(int i = 0; i < 8; ++i)
    {
        now += 2000;
        m.tick(now);
    }
    RTF_ASSERT(state_of(m.evaluate(now), "write_delay") == EL);
    // Growing 100 ms a tick over 10 s or more: high.
    for(int i = 0; i < 7; ++i)
    {
        now += 2000;
        delay += 100;
        m.tick(now);
    }
    RTF_ASSERT(state_of(m.evaluate(now), "write_delay") == HI);
    // One dip ends the growth.
    now += 2000;
    delay -= 300;
    m.tick(now);
    RTF_ASSERT(state_of(m.evaluate(now), "write_delay") == EL);
    // A tick with nothing written (the source says none) is no delay, not the last one kept.
    now += 2000;
    delay = -1;
    m.tick(now);
    RTF_ASSERT(state_of(m.evaluate(now), "write_delay") == OK);
    // Without the source it is unknown.
    r_pressure_monitor none{r_pressure_sources()};
    none.tick(1000);
    RTF_ASSERT(state_of(none.evaluate(1000), "write_delay") == U);
}

void test_r_pressure::test_monitor_detection_against_its_baseline()
{
    r_pressure_monitor m{r_pressure_sources()};
    RTF_ASSERT(state_of(m.evaluate(0), "detection_slow") == U);
    // Ten minutes at 100 ms, a reading every second.
    int64_t now = 0;
    for(int minute = 0; minute < 10; ++minute)
        for(int i = 0; i < 60; ++i)
        {
            now += 1000;
            m.note_detection_latency(100, now);
            if(i % 2 == 0)
                m.tick(now);
        }
    RTF_ASSERT(state_of(m.evaluate(now), "detection_slow") == OK);
    // A minute at 400 ms (4 times its baseline): high.
    for(int i = 0; i < 60; ++i)
    {
        now += 1000;
        m.note_detection_latency(400, now);
        if(i % 2 == 0)
            m.tick(now);
    }
    RTF_ASSERT(state_of(m.evaluate(now), "detection_slow") == HI);
    // 180 ms is 1.8 times: elevated.
    for(int i = 0; i < 61; ++i)
    {
        now += 1000;
        m.note_detection_latency(180, now);
        if(i % 2 == 0)
            m.tick(now);
    }
    RTF_ASSERT(state_of(m.evaluate(now), "detection_slow") == EL);
}

// With under five minutes of history there is no baseline, so a slow minute is judged only against 2 s.
void test_r_pressure::test_monitor_detection_needs_a_baseline_first()
{
    r_pressure_monitor m{r_pressure_sources()};
    int64_t now = 0;
    for(int minute = 0; minute < 3; ++minute)
        for(int i = 0; i < 60; ++i)
        {
            now += 1000;
            m.note_detection_latency(100, now);
            if(i % 2 == 0)
                m.tick(now);
        }
    for(int i = 0; i < 60; ++i)
    {
        now += 1000;
        m.note_detection_latency(400, now);
        if(i % 2 == 0)
            m.tick(now);
    }
    RTF_ASSERT(state_of(m.evaluate(now), "detection_slow") == OK);
}

void test_r_pressure::test_monitor_json()
{
    fake_cpu c;
    r_pressure_sources s = cpu_only(c);
    s.hw_sessions = [](int& used, int& limit) { used = 3; limit = 3; return true; };
    r_pressure_monitor m(s);
    c.busy = 0.5;
    c.run(m, 10000);
    auto j = json::parse(m.json(c.at));
    RTF_ASSERT(j["level"] == "high");                        // the encoder is at its limit
    RTF_ASSERT(j["reasons"].size() == 1 && j["reasons"][0]["signal"] == "hw_encoder" && j["reasons"][0]["text"] == "the hardware encoder is all in use");
    RTF_ASSERT(j["signals"].size() == 6);
    RTF_ASSERT(j["cpu"].is_number() && j["cpu"].get<double>() > 0.49 && j["cpu"].get<double>() < 0.51);
    RTF_ASSERT(j["cpu_p95_24h"].is_null());                  // under a minute of history
    RTF_ASSERT(j["cores"].get<int>() >= 1);
    RTF_ASSERT(j["hw_sessions"]["used"] == 3 && j["hw_sessions"]["limit"] == 3);
    // Nothing known: ok, null where unknown.
    r_pressure_monitor none{r_pressure_sources()};
    auto n = json::parse(none.json(0));
    RTF_ASSERT(n["level"] == "ok" && n["reasons"].empty() && n["cpu"].is_null() && n["hw_sessions"].is_null());
    for(const auto& sig : n["signals"])
        RTF_ASSERT(sig["state"] == "unknown");
}

void test_r_pressure::test_write_delay_recorder()
{
    r_storage::take_write_delay_max();               // clear anything left by another test
    RTF_ASSERT(r_storage::take_write_delay_max() == -1);
    r_storage::note_write_delay(40);
    r_storage::note_write_delay(900);
    r_storage::note_write_delay(120);
    r_storage::note_write_delay(-5);
    RTF_ASSERT(r_storage::take_write_delay_max() == 900);
    RTF_ASSERT(r_storage::take_write_delay_max() == -1);      // taken: cleared
    r_storage::note_write_delay(-5);
    RTF_ASSERT(r_storage::take_write_delay_max() == 0);
}

void test_r_pressure::test_real_cpu_reading()
{
    r_cpu_ticks a, b;
    RTF_ASSERT(read_cpu_ticks(a));
    RTF_ASSERT(a.total > 0 && a.busy <= a.total);
    RTF_ASSERT(read_cpu_ticks(b));
    RTF_ASSERT(b.total >= a.total);
    RTF_ASSERT(cpu_core_count() >= 1);
}
