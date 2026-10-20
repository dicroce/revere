#include "r_cameras/r_camera_events_base.h"
#include "r_utils/r_logger.h"
#include <chrono>

using namespace r_cameras;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

static int64_t _now_ms()
{
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

r_camera_events_base::r_camera_events_base(const r_disco::r_camera& camera, transition_fn on_transition, const string& source) :
    _camera(camera),
    _on_transition(std::move(on_transition))
{
    auto tag = camera.friendly_name.is_null() ? camera.id : camera.friendly_name.value();
    _log_prefix = source + " events [" + tag + "]: ";
    _status.source = source;
}

r_camera_events_base::~r_camera_events_base() noexcept
{
    _join();
}

void r_camera_events_base::start()
{
    _keep_running = true;
    _th = thread(&r_camera_events_base::_entry_point, this);
}

void r_camera_events_base::request_stop()
{
    {
        lock_guard<mutex> g(_lock);
        _keep_running = false;
    }
    _cv.notify_all();
}

void r_camera_events_base::_join()
{
    request_stop();
    if (_th.joinable())
        _th.join();
}

void r_camera_events_base::_entry_point()
{
    try
    {
        _run();
    }
    catch (const exception& e)
    {
        R_LOG_ERROR("%sstopped by an error: %s", _log_prefix.c_str(), e.what());
    }
    lock_guard<mutex> g(_lock);
    _status.up = false;
    // A source that gave up by itself ("no events") keeps saying why.
    if (!_keep_running)
        _status.state = "stopped";
}

bool r_camera_events_base::_wait(int seconds_to_wait)
{
    unique_lock<mutex> g(_lock);
    _cv.wait_for(g, seconds(seconds_to_wait), [this]() { return !_keep_running.load(); });
    return _keep_running.load();
}

r_camera_events_status r_camera_events_base::status() const
{
    lock_guard<mutex> g(_lock);
    return _status;
}

vector<r_camera_span> r_camera_events_base::spans(int64_t since_ms) const
{
    lock_guard<mutex> g(_lock);
    vector<r_camera_span> out;
    for (auto& s : _spans)
        if (s.start_ms >= since_ms)
            out.push_back(s);
    return out;
}

void r_camera_events_base::_update(const function<void(r_camera_events_status&)>& change)
{
    lock_guard<mutex> g(_lock);
    change(_status);
}

bool r_camera_events_base::_stale() const
{
    lock_guard<mutex> g(_lock);
    auto now = _now_ms();
    for (auto& s : _spans)
        if (s.end_ms == 0 && now - s.start_ms > MAX_EVENT_MS)
            return true;
    return false;
}

void r_camera_events_base::_apply(const vector<r_camera_transition>& transitions)
{
    if (transitions.empty())
        return;
    {
        auto now = _now_ms();
        lock_guard<mutex> g(_lock);
        for (auto& t : transitions)
        {
            R_LOG_INFO("%s%s %s", _log_prefix.c_str(), t.what.c_str(), t.active ? "start" : "end");
            if (t.active)
            {
                _spans.push_back({t.what, now, 0});
                if (_spans.size() > MAX_SPANS)
                    _spans.pop_front();
            }
            else
            {
                // Close the newest open span of this kind.
                for (auto it = _spans.rbegin(); it != _spans.rend(); ++it)
                    if (it->what == t.what && it->end_ms == 0) { it->end_ms = now; break; }
            }
        }
        _status.last_event_ms = now;
    }
    if (_on_transition)
        for (auto& t : transitions)
            _on_transition(t);
}
