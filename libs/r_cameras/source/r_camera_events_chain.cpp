#include "r_cameras/r_camera_events_chain.h"
#include "r_utils/r_logger.h"
#include <algorithm>
#include <chrono>

using namespace r_cameras;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

static int64_t _now_ms()
{
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

r_camera_events_chain::r_camera_events_chain(factory_fn primary, factory_fn fallback, probe_fn probe,
                                             transition_fn on_transition, const string& tag) :
    _make_primary(std::move(primary)),
    _make_fallback(std::move(fallback)),
    _probe(std::move(probe)),
    _on_transition(std::move(on_transition)),
    _tag(tag)
{
}

r_camera_events_chain::~r_camera_events_chain() noexcept
{
    request_stop();
    if (_th.joinable())
        _th.join();
    // The sources' destructors wait for their threads (and end open events).
    unique_ptr<r_camera_events> fallback, primary;
    {
        lock_guard<mutex> g(_lock);
        fallback = std::move(_fallback);
        primary = std::move(_primary);
    }
}

void r_camera_events_chain::start_without_thread()
{
    lock_guard<mutex> g(_lock);
    _running = true;
    _primary = _make_primary(_on_transition);
    if (_primary)
        _primary->start();
}

void r_camera_events_chain::start()
{
    start_without_thread();
    _th = thread(&r_camera_events_chain::_entry_point, this);
}

void r_camera_events_chain::request_stop()
{
    {
        lock_guard<mutex> g(_lock);
        _running = false;
        if (_primary)
            _primary->request_stop();
        if (_fallback)
            _fallback->request_stop();
    }
    _cv.notify_all();
}

void r_camera_events_chain::_entry_point()
{
    while (true)
    {
        {
            unique_lock<mutex> g(_lock);
            _cv.wait_for(g, milliseconds(CHECK_MS), [this]() { return !_running; });
            if (!_running)
                return;
        }
        try
        {
            tick(_now_ms());
        }
        catch (const exception& e)
        {
            R_LOG_ERROR("camera events [%s]: %s", _tag.c_str(), e.what());
        }
    }
}

bool r_camera_events_chain::fallback_running() const
{
    lock_guard<mutex> g(_lock);
    return (bool)_fallback;
}

void r_camera_events_chain::_retire_fallback()
{
    unique_ptr<r_camera_events> old;
    {
        lock_guard<mutex> g(_lock);
        if (!_fallback)
            return;
        for (auto& s : _fallback->spans(0))
            _retired.push_back(s);
        while (_retired.size() > MAX_RETIRED_SPANS)
            _retired.pop_front();
        old = std::move(_fallback);
    }
    // Outside the lock: stopping waits for its thread, and status() mustn't wait on that.
    old.reset();
}

void r_camera_events_chain::tick(int64_t now_ms)
{
    r_camera_events* primary;
    bool fallback;
    {
        lock_guard<mutex> g(_lock);
        if (!_running || !_primary)
            return;
        primary = _primary.get();
        fallback = (bool)_fallback;
    }

    if (primary->status().up)
    {
        _primary_down_since = -1;
        if (fallback)
        {
            R_LOG_INFO("camera events [%s]: %s events are back; stopping the fallback", _tag.c_str(), primary->status().source.c_str());
            _retire_fallback();
        }
        return;
    }

    if (_primary_down_since < 0)
        _primary_down_since = now_ms;
    if (fallback || !_make_fallback || now_ms - _primary_down_since < FALLBACK_AFTER_MS)
        return;

    if (_probe_result != 1 && (_probe_result == -1 || now_ms - _last_probe_ms >= PROBE_EVERY_MS))
    {
        _last_probe_ms = now_ms;
        _probe_result = (_probe && _probe()) ? 1 : 0;
    }
    if (_probe_result != 1)
        return;

    auto next = _make_fallback(_on_transition);
    if (!next)
        return;
    {
        lock_guard<mutex> g(_lock);
        if (!_running)
            return;
        R_LOG_INFO("camera events [%s]: %s events have been down %lld s; using the fallback", _tag.c_str(),
                   primary->status().source.c_str(), (long long)((now_ms - _primary_down_since) / 1000));
        _fallback = std::move(next);
        _fallback->start();
    }
}

r_camera_events_status r_camera_events_chain::status() const
{
    lock_guard<mutex> g(_lock);
    if (_fallback)
        return _fallback->status();
    if (_primary)
        return _primary->status();
    return r_camera_events_status();
}

vector<r_camera_span> r_camera_events_chain::spans(int64_t since_ms) const
{
    vector<r_camera_span> out;
    {
        lock_guard<mutex> g(_lock);
        for (auto& s : _retired)
            if (s.start_ms >= since_ms)
                out.push_back(s);
        if (_primary)
            for (auto& s : _primary->spans(since_ms))
                out.push_back(s);
        if (_fallback)
            for (auto& s : _fallback->spans(since_ms))
                out.push_back(s);
    }
    stable_sort(out.begin(), out.end(), [](const r_camera_span& a, const r_camera_span& b) { return a.start_ms < b.start_ms; });
    return out;
}
