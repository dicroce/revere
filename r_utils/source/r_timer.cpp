
#include "r_utils/r_timer.h"
#include <algorithm>
#include <ctime>

using namespace r_utils;
using namespace std;
using namespace std::chrono;

void r_timer::add_timed_event(const std::chrono::steady_clock::time_point& now, const std::chrono::milliseconds& interval, r_timer_cb cb, bool initial_fire, bool one_shot)
{
    r_timed_event te;
    te.interval = interval;
    te.cb = cb;
    te.one_shot = one_shot;
    te.next_fire_time = (initial_fire) ? now : now + interval;
    _timed_events.insert(
        upper_bound(begin(_timed_events),end(_timed_events),
            te, [](const r_timed_event& a, const r_timed_event& b) {
                return a.next_fire_time < b.next_fire_time;
            }
        ),
        te
    );
}

milliseconds r_timer::update(const std::chrono::milliseconds& max_sleep, const steady_clock::time_point& now)
{
    vector<r_timed_event> timed_events;
    for(auto te : _timed_events)
    {
        bool done = te.update(now);
        if(!done)
            timed_events.push_back(te);
    }
    _timed_events = timed_events;

    if(!_timed_events.empty())
    {
        auto next_event = _timed_events.front();

        auto next_event_delta = duration_cast<milliseconds>(next_event.next_fire_time - now);

        return (next_event_delta < max_sleep) ? next_event_delta : max_sleep;
    }
    else return max_sleep;
}

bool r_timer::r_timed_event::update(const std::chrono::steady_clock::time_point& now)
{
    auto should_fire_at = next_fire_time;

    while(should_fire_at <= now)
    {
        should_fire_at += interval;
        cb();
        if(one_shot)
            return true;
    }

    next_fire_time = should_fire_at;

    return false;
}
