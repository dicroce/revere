#include "r_vss/r_pressure.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>
#include <chrono>

using namespace r_vss;
using namespace std;
using njson = nlohmann::json;

const char* r_vss::pressure_level_name(pressure_level level)
{
    switch(level)
    {
        case pressure_level::elevated: return "elevated";
        case pressure_level::high: return "high";
        default: return "ok";
    }
}

const char* r_vss::signal_state_name(signal_state state)
{
    switch(state)
    {
        case signal_state::ok: return "ok";
        case signal_state::elevated: return "elevated";
        case signal_state::high: return "high";
        default: return "unknown";
    }
}

static bool at_least_elevated(signal_state s)
{
    return s == signal_state::elevated || s == signal_state::high;
}

r_pressure_result r_vss::evaluate_pressure(const r_pressure_inputs& in)
{
    r_pressure_result r;
    auto add = [&](const char* id, signal_state state, const char* elevated_text, const char* high_text) {
        r_pressure_signal s;
        s.id = id;
        s.state = state;
        if(state == signal_state::elevated)
            s.text = elevated_text;
        else if(state == signal_state::high)
            s.text = high_text;
        r.signals.push_back(s);
    };

    // Motion detection falling behind.
    {
        auto state = signal_state::unknown;
        if(in.motion_known)
        {
            state = signal_state::ok;
            if((in.motion_skip_age_ms >= 0 && in.motion_skip_age_ms <= PRESSURE_MOTION_RECENT_MS) || in.motion_queue > PRESSURE_MOTION_QUEUE_ELEVATED)
                state = signal_state::elevated;
            if(in.motion_drop_age_ms >= 0 && in.motion_drop_age_ms <= PRESSURE_MOTION_RECENT_MS)
                state = signal_state::high;
        }
        add("motion_drop", state, "motion detection is falling behind", "motion detection is dropping frames");
    }

    // Detection slower than usual.
    {
        auto state = signal_state::unknown;
        if(in.detection_known)
        {
            state = signal_state::ok;
            const bool have_baseline = in.detection_baseline_ms > 0.0;
            if(have_baseline && in.detection_p90_ms > in.detection_baseline_ms * PRESSURE_DETECTION_ELEVATED_RATIO)
                state = signal_state::elevated;
            if((have_baseline && in.detection_p90_ms > in.detection_baseline_ms * PRESSURE_DETECTION_HIGH_RATIO) ||
               in.detection_p90_ms > (double)PRESSURE_DETECTION_HIGH_MS)
                state = signal_state::high;
        }
        add("detection_slow", state, "motion detection is slower than usual", "motion detection is much slower than usual");
    }

    // Recording writes behind. Growing counts only past the elevated line: a delay of a few ms drifting up is not a backlog.
    {
        auto state = signal_state::unknown;
        if(in.writes_known)
        {
            state = signal_state::ok;
            if(in.write_delay_ms > PRESSURE_WRITE_ELEVATED_MS)
                state = signal_state::elevated;
            if(in.write_delay_ms > PRESSURE_WRITE_HIGH_MS || (in.write_delay_growing && in.write_delay_ms > PRESSURE_WRITE_ELEVATED_MS))
                state = signal_state::high;
        }
        add("write_delay", state, "recording writes are behind", "recording writes are far behind");
    }

    // The CPU comes before the cameras behind live, which counts only while it is elevated.
    auto cpu_state = signal_state::unknown;
    if(in.cpu_known)
    {
        cpu_state = signal_state::ok;
        if(in.cpu_elevated_for_ms >= r_cpu_sampler::ELEVATED_FOR_MS)
            cpu_state = signal_state::elevated;
        if(in.cpu_high_for_ms >= r_cpu_sampler::HIGH_FOR_MS)
            cpu_state = signal_state::high;
    }

    // Cameras behind live: a slow network lags a camera too, so only while the CPU is also busy.
    {
        auto state = signal_state::unknown;
        if(in.lag_known)
            state = (in.lag_camera_flagged && at_least_elevated(cpu_state)) ? signal_state::elevated : signal_state::ok;
        add("camera_lag", state, "a camera is falling behind live while the computer is busy", "");
    }

    add("cpu_busy", cpu_state, "the computer is busy", "the computer is very busy");

    // Hardware encoder sessions.
    {
        auto state = signal_state::unknown;
        if(in.hw_known && in.hw_limit > 0)
        {
            state = signal_state::ok;
            if(in.hw_used >= in.hw_limit - PRESSURE_HW_ELEVATED_FREE)
                state = signal_state::elevated;
            if(in.hw_used >= in.hw_limit)
                state = signal_state::high;
        }
        add("hw_encoder", state, "the hardware encoder is nearly all in use", "the hardware encoder is all in use");
    }

    for(const auto& s : r.signals)
    {
        if(s.state == signal_state::high)
            r.level = pressure_level::high;
        else if(s.state == signal_state::elevated && r.level == pressure_level::ok)
            r.level = pressure_level::elevated;
    }
    return r;
}

namespace
{
double percentile(vector<double> v, double p)
{
    if(v.empty())
        return 0.0;
    sort(v.begin(), v.end());
    size_t rank = (size_t)((double)v.size() * p + 0.999999);
    if(rank < 1)
        rank = 1;
    return v[min(rank, v.size()) - 1];
}

int64_t steady_ms_now()
{
    return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now().time_since_epoch()).count();
}
}

r_pressure_monitor::r_pressure_monitor(r_pressure_sources sources) :
    _sources(std::move(sources))
{
}

r_pressure_monitor::~r_pressure_monitor()
{
    stop();
}

void r_pressure_monitor::start()
{
    if(_running.exchange(true))
        return;
    _thread = thread(&r_pressure_monitor::_entry_point, this);
}

void r_pressure_monitor::stop()
{
    if(!_running.exchange(false))
        return;
    if(_thread.joinable())
        _thread.join();
}

void r_pressure_monitor::_entry_point()
{
    while(_running)
    {
        tick(steady_ms_now());
        // In short sleeps so stop() doesn't wait a whole tick.
        for(int64_t waited = 0; waited < TICK_MS && _running; waited += 100)
            this_thread::sleep_for(chrono::milliseconds(100));
    }
}

void r_pressure_monitor::note_motion_skip(int64_t now_ms)
{
    lock_guard<mutex> g(_lock);
    _last_skip_ms = now_ms;
}

void r_pressure_monitor::note_motion_dropped(size_t count, int64_t now_ms)
{
    if(count == 0)
        return;
    lock_guard<mutex> g(_lock);
    _last_drop_ms = now_ms;
}

void r_pressure_monitor::note_detection_latency(int64_t ms, int64_t now_ms)
{
    lock_guard<mutex> g(_lock);
    _detect_recent.push_back({now_ms, ms < 0 ? 0 : ms});
    // Bounded: a minute's worth, and never more than this many however fast they come.
    while(_detect_recent.size() > 20000)
        _detect_recent.pop_front();
}

void r_pressure_monitor::tick(int64_t now_ms)
{
    // The sources are called outside the lock: they take their own.
    r_cpu_ticks ticks;
    bool have_ticks = _sources.cpu_ticks && _sources.cpu_ticks(ticks);
    int64_t write_delay = _sources.take_write_delay_max ? _sources.take_write_delay_max() : -2;
    size_t queue = 0;
    bool have_queue = false;
    if(_sources.motion_queue_size)
    {
        queue = _sources.motion_queue_size();
        have_queue = true;
    }
    bool lag = false, have_lag = false;
    if(_sources.camera_flagged_lagging)
    {
        lag = _sources.camera_flagged_lagging();
        have_lag = true;
    }
    int used = 0, limit = 0;
    bool have_hw = _sources.hw_sessions && _sources.hw_sessions(used, limit);

    lock_guard<mutex> g(_lock);
    _now_ms_last = now_ms;
    if(have_ticks)
        _cpu.add(now_ms, ticks);
    if(write_delay != -2)
    {
        _writes_seen = true;
        _write_delay_ms = write_delay < 0 ? 0 : write_delay;
        _write_ticks.push_back({now_ms, _write_delay_ms});
        while(!_write_ticks.empty() && now_ms - _write_ticks.front().first > PRESSURE_WRITE_GROWING_MS + TICK_MS)
            _write_ticks.pop_front();
    }
    if(have_queue)
    {
        _motion_seen = true;
        _motion_queue = queue;
    }
    _lag_known = have_lag;
    _lag_flagged = lag;
    _hw_known = have_hw;
    _hw_used = used;
    _hw_limit = limit;

    // The last minute's detection latencies, and each finished minute's 90th percentile for the hour's baseline.
    while(!_detect_recent.empty() && now_ms - _detect_recent.front().first > 60000)
        _detect_recent.pop_front();
    const int64_t minute = now_ms / 60000;
    if(_detect_minute >= 0 && minute != _detect_minute && !_detect_recent.empty())
    {
        vector<double> v;
        for(const auto& d : _detect_recent)
            v.push_back((double)d.second);
        _detect_minutes.push_back(percentile(v, 0.9));
        while(_detect_minutes.size() > 60)
            _detect_minutes.pop_front();
    }
    _detect_minute = minute;
}

r_pressure_inputs r_pressure_monitor::inputs(int64_t now_ms) const
{
    lock_guard<mutex> g(_lock);
    r_pressure_inputs in;

    in.motion_known = _motion_seen;
    in.motion_queue = _motion_queue;
    in.motion_skip_age_ms = _last_skip_ms < 0 ? -1 : now_ms - _last_skip_ms;
    in.motion_drop_age_ms = _last_drop_ms < 0 ? -1 : now_ms - _last_drop_ms;

    if(!_detect_recent.empty())
    {
        vector<double> v;
        for(const auto& d : _detect_recent)
            v.push_back((double)d.second);
        in.detection_known = true;
        in.detection_p90_ms = percentile(v, 0.9);
        // A baseline needs a few minutes of history; the median of them, so one bad minute doesn't move it.
        if(_detect_minutes.size() >= 5)
            in.detection_baseline_ms = percentile(vector<double>(_detect_minutes.begin(), _detect_minutes.end()), 0.5);
    }

    in.writes_known = _writes_seen;
    in.write_delay_ms = _write_delay_ms;
    // Growing: ticks spanning PRESSURE_WRITE_GROWING_MS, never down, and higher at the end than the start.
    if(_write_ticks.size() >= 2 && _write_ticks.back().first - _write_ticks.front().first >= PRESSURE_WRITE_GROWING_MS)
    {
        bool never_down = true;
        for(size_t i = 1; i < _write_ticks.size(); ++i)
            if(_write_ticks[i].second < _write_ticks[i - 1].second)
                never_down = false;
        in.write_delay_growing = never_down && _write_ticks.back().second > _write_ticks.front().second;
    }

    in.lag_known = _lag_known;
    in.lag_camera_flagged = _lag_flagged;

    in.cpu_known = _cpu.has_sample();
    in.cpu_elevated_for_ms = _cpu.elevated_for_ms(_now_ms_last);
    in.cpu_high_for_ms = _cpu.high_for_ms(_now_ms_last);

    in.hw_known = _hw_known;
    in.hw_used = _hw_used;
    in.hw_limit = _hw_limit;
    return in;
}

r_pressure_result r_pressure_monitor::evaluate(int64_t now_ms) const
{
    return evaluate_pressure(inputs(now_ms));
}

string r_pressure_monitor::json(int64_t now_ms) const
{
    const auto in = inputs(now_ms);
    const auto result = evaluate_pressure(in);

    njson j;
    j["level"] = pressure_level_name(result.level);
    j["reasons"] = njson::array();
    j["signals"] = njson::array();
    for(const auto& s : result.signals)
    {
        j["signals"].push_back({{"id", s.id}, {"state", signal_state_name(s.state)}});
        if(!s.text.empty())
            j["reasons"].push_back({{"signal", s.id}, {"text", s.text}});
    }
    {
        lock_guard<mutex> g(_lock);
        const double busy = _cpu.busy();
        const double p95 = _cpu.busy_p95_24h();
        j["cpu"] = busy < 0 ? njson(nullptr) : njson(busy);
        j["cpu_p95_24h"] = p95 < 0 ? njson(nullptr) : njson(p95);
    }
    j["cores"] = cpu_core_count();
    if(in.hw_known && in.hw_limit > 0)
        j["hw_sessions"] = {{"used", in.hw_used}, {"limit", in.hw_limit}};
    else
        j["hw_sessions"] = nullptr;
    return j.dump();
}
