#include "r_vss/r_lag_watchdog.h"
#include <algorithm>
#include <cmath>

using namespace r_vss;
using namespace std;

const char* const r_vss::LAG_FLAG_MESSAGE = "network can't keep up with this stream: use the sub-stream or a wired link";

r_lag_watchdog::r_lag_event r_lag_watchdog::frame(int64_t camera_ms, int64_t arrival_ms)
{
    double offset = (double)(arrival_ms - camera_ms);

    if(!_anchored || camera_ms < _newest_camera_ms - JUMP_MS)
    {
        // Connect, or the camera's clock went back (a clock jump, a stream
        // restart): start the measure again from this frame.
        bool jumped = _anchored;
        _anchored = true;
        _baseline = offset;
        _newest_camera_ms = camera_ms;
        _last_arrival_ms = arrival_ms;
        _lag_ms = 0;
        _reset_episode();
        _samples.clear();
        _samples.emplace_back(arrival_ms, camera_ms);
        if(jumped)
            ++_clock_jumps;
        return jumped ? lag_clock_jump : lag_none;
    }

    auto elapsed = max<int64_t>(0, arrival_ms - _last_arrival_ms);
    _baseline = min(offset, _baseline + (double)elapsed * DRIFT_ALLOWANCE_PPM / 1e6);
    _lag_ms = (int64_t)llround(offset - _baseline);
    _newest_camera_ms = max(_newest_camera_ms, camera_ms);
    _last_arrival_ms = arrival_ms;

    if(_samples.empty() || arrival_ms - _samples.back().first >= RATE_SAMPLE_MS)
        _samples.emplace_back(arrival_ms, camera_ms);
    while(_samples.size() > 2 && arrival_ms - _samples.front().first > RATE_WINDOW_MS)
        _samples.pop_front();

    auto event = lag_none;

    if(_lag_ms > LAG_MS)
    {
        if(_over_since < 0)
        {
            _over_since = arrival_ms;
            _peak_ms = 0;
        }
        _peak_ms = max(_peak_ms, _lag_ms);
        if(!_lagging && arrival_ms - _over_since >= LAG_SUSTAIN_MS)
        {
            _lagging = true;
            ++_episodes;
            event = lag_started;
        }
    }
    else if(_lag_ms < CAUGHT_UP_MS && _over_since >= 0)
    {
        // Between CAUGHT_UP_MS and LAG_MS the episode carries on: a backlog
        // that is draining slowly hasn't caught up yet.
        if(_lagging)
        {
            _last_episode_ms = arrival_ms - _over_since;
            event = lag_caught_up;
        }
        _reset_episode();
    }

    if(_lag_ms >= RECONNECT_LAG_MS)
    {
        if(_reconnect_since < 0)
        {
            _reconnect_since = arrival_ms;
            _reconnect_peak = 0;
        }
        _reconnect_peak = max(_reconnect_peak, _lag_ms);
    }
    else
        _reconnect_since = -1;

    return event;
}

void r_lag_watchdog::_reset_episode()
{
    _over_since = -1;
    _reconnect_since = -1;
    _lagging = false;
}

int64_t r_lag_watchdog::behind_for_ms(int64_t arrival_ms) const
{
    return (_over_since < 0) ? 0 : max<int64_t>(0, arrival_ms - _over_since);
}

bool r_lag_watchdog::wants_reconnect() const
{
    // Over RECONNECT_LAG_MS, but draining: leave it, unless it's stuck.
    if(_reconnect_since >= 0 && _last_arrival_ms - _reconnect_since >= RECONNECT_SUSTAIN_MS &&
       _lag_ms >= _reconnect_peak - DRAIN_MARGIN_MS)
        return true;
    return _over_since >= 0 && _last_arrival_ms - _over_since >= STUCK_MS;
}

double r_lag_watchdog::rate() const
{
    if(_samples.size() < 2)
        return 1.0;
    auto arrival_span = _samples.back().first - _samples.front().first;
    if(arrival_span < 1000)
        return 1.0;
    return (double)(_samples.back().second - _samples.front().second) / (double)arrival_span;
}

r_lag_snapshot r_lag_watchdog::snapshot(int64_t now_ms) const
{
    r_lag_snapshot s;
    s.lag_ms = _lag_ms;
    s.peak_lag_ms = _peak_ms;
    s.behind_for_ms = behind_for_ms(now_ms);
    s.rate = rate();
    s.lagging = _lagging;
    s.wants_reconnect = wants_reconnect();
    s.episodes = _episodes;
    return s;
}

bool r_lag_reconnects::try_reconnect(const string& camera_id, int64_t now_ms)
{
    lock_guard<mutex> g(_mtx);
    auto& s = _cameras[camera_id];
    if(now_ms < s.next_allowed)
        return false;

    if(!s.times.empty() && now_ms - s.times.back() > RESET_MS)
        s.hold_ms = 0;
    s.hold_ms = (s.hold_ms == 0) ? (int64_t)FIRST_HOLD_MS : min<int64_t>(s.hold_ms * 2, MAX_HOLD_MS);
    s.next_allowed = now_ms + s.hold_ms;

    s.times.push_back(now_ms);
    while(!s.times.empty() && now_ms - s.times.front() > FLAG_WINDOW_MS)
        s.times.pop_front();
    return true;
}

int64_t r_lag_reconnects::held_off_ms(const string& camera_id, int64_t now_ms) const
{
    lock_guard<mutex> g(_mtx);
    auto it = _cameras.find(camera_id);
    if(it == _cameras.end())
        return 0;
    return max<int64_t>(0, it->second.next_allowed - now_ms);
}

int r_lag_reconnects::recent(const string& camera_id, int64_t now_ms) const
{
    lock_guard<mutex> g(_mtx);
    auto it = _cameras.find(camera_id);
    if(it == _cameras.end())
        return 0;
    return (int)count_if(it->second.times.begin(), it->second.times.end(),
                         [&](int64_t t){ return now_ms - t <= FLAG_WINDOW_MS; });
}

bool r_lag_reconnects::flagged(const string& camera_id, int64_t now_ms) const
{
    return recent(camera_id, now_ms) >= FLAG_RECONNECTS;
}

uint64_t r_newest_frame_gate::posted(const string& camera_id)
{
    lock_guard<mutex> g(_mtx);
    return ++_newest[camera_id];
}

bool r_newest_frame_gate::skip(const string& camera_id, uint64_t seq, int64_t waited_ms) const
{
    lock_guard<mutex> g(_mtx);
    auto it = _newest.find(camera_id);
    return it != _newest.end() && seq < it->second && waited_ms > STALE_MS;
}

void r_newest_frame_gate::forget(const string& camera_id)
{
    lock_guard<mutex> g(_mtx);
    _newest.erase(camera_id);
}
