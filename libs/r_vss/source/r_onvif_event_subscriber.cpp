#include "r_vss/r_onvif_event_subscriber.h"
#include "r_onvif/r_onvif_session.h"
#include "r_utils/r_logger.h"
#include <chrono>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

static const int PULL_TIMEOUT_S = 2;       // short, so a stop is never waiting on a long pull
static const int TERMINATION_S = 120;
static const size_t MAX_SPANS = 500;
static const int64_t MAX_EVENT_MS = 15 * 60 * 1000;  // longer than any real motion: an end was missed

static int64_t _now_ms()
{
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool r_onvif_event_subscriber::wanted_for(const r_disco::r_camera& camera)
{
    return !camera.ipv4.is_null() && !camera.ipv4.value().empty() &&
           !camera.xaddrs.is_null() && !camera.xaddrs.value().empty();
}

r_onvif_event_subscriber::r_onvif_event_subscriber(const r_disco::r_camera& camera) :
    _camera(camera)
{
    _tag = camera.friendly_name.is_null() ? camera.id : camera.friendly_name.value();
}

r_onvif_event_subscriber::~r_onvif_event_subscriber() noexcept
{
    request_stop();
    if (_th.joinable())
        _th.join();
}

void r_onvif_event_subscriber::start()
{
    _running = true;
    _th = thread(&r_onvif_event_subscriber::_entry_point, this);
}

void r_onvif_event_subscriber::request_stop()
{
    {
        lock_guard<mutex> g(_lock);
        _running = false;
    }
    _cv.notify_all();
}

bool r_onvif_event_subscriber::_wait(int seconds_to_wait)
{
    unique_lock<mutex> g(_lock);
    _cv.wait_for(g, seconds(seconds_to_wait), [this]() { return !_running.load(); });
    return _running.load();
}

r_onvif_events_status r_onvif_event_subscriber::status() const
{
    lock_guard<mutex> g(_lock);
    return _status;
}

vector<r_onvif_span> r_onvif_event_subscriber::spans(int64_t since_ms) const
{
    lock_guard<mutex> g(_lock);
    vector<r_onvif_span> out;
    for (auto& s : _spans)
        if (s.start_ms >= since_ms)
            out.push_back(s);
    return out;
}

void r_onvif_event_subscriber::_apply(const vector<r_onvif_transition>& transitions)
{
    if (transitions.empty())
        return;
    auto now = _now_ms();
    lock_guard<mutex> g(_lock);
    for (auto& t : transitions)
    {
        R_LOG_INFO("onvif events [%s]: %s %s", _tag.c_str(), t.what.c_str(), t.active ? "start" : "end");
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
    _status.motion_topic = _tracker.motion_topic();
    _status.classes = _tracker.classes();
}

void r_onvif_event_subscriber::_entry_point()
{
    auto should_continue = [this]() { return _running.load(); };
    int backoff_s = 10;
    r_nullable<int> port_hint = _camera.onvif_port;

    while (_running)
    {
        string stage = "finding the device service";
        try
        {
            auto ep = r_onvif::find_device_service(_camera.ipv4.value(), _camera.xaddrs.value(), port_hint, _camera.onvif_protocol, should_continue);
            if (!_running)
                break;
            if (ep.is_null())
                throw runtime_error("no ONVIF device service answers");
            port_hint = ep.value().port;

            stage = "connecting";
            r_onvif::r_onvif_cam cam(ep.value().host, ep.value().port, ep.value().protocol, ep.value().uri,
                                     _camera.rtsp_username, _camera.rtsp_password);
            stage = "GetCapabilities";
            auto events = cam.get_events_service(cam.get_camera_capabilities());
            if (events.is_null())
            {
                {
                    lock_guard<mutex> g(_lock);
                    _status.state = "no events";
                    _status.port = ep.value().port;
                }
                R_LOG_INFO("onvif events [%s]: the camera has no events service", _tag.c_str());
                return;
            }

            stage = "CreatePullPointSubscription";
            auto pp = cam.create_pull_point_subscription(events.value(), TERMINATION_S);
            {
                lock_guard<mutex> g(_lock);
                _status.state = "subscribed";
                _status.port = ep.value().port;
                _status.last_error.clear();
            }
            R_LOG_INFO("onvif events [%s]: subscribed on port %d", _tag.c_str(), ep.value().port);
            backoff_s = 10;

            auto renewed = steady_clock::now();
            while (_running)
            {
                stage = "PullMessages";
                _apply(_tracker.process(cam.pull_messages(pp, PULL_TIMEOUT_S, 100)));

                // An event open far longer than anything real means its end was missed.
                {
                    bool stale = false;
                    {
                        lock_guard<mutex> g(_lock);
                        auto now = _now_ms();
                        for (auto& s : _spans)
                            if (s.end_ms == 0 && now - s.start_ms > MAX_EVENT_MS)
                                stale = true;
                    }
                    if (stale)
                        _apply(_tracker.close_all());
                }

                if (steady_clock::now() - renewed > seconds(TERMINATION_S / 2))
                {
                    stage = "Renew";
                    cam.renew(pp, TERMINATION_S);
                    renewed = steady_clock::now();
                }
            }
            try { cam.unsubscribe(pp); } catch (...) {}
        }
        catch (const exception& e)
        {
            string what = e.what();
            auto nl = what.find('\n');
            if (nl != string::npos)
                what = what.substr(0, nl);
            {
                lock_guard<mutex> g(_lock);
                _status.state = "down";
                ++_status.failures;
                _status.last_error = stage + ": " + what;
            }
            R_LOG_WARNING("onvif events [%s]: failed %s: %s (retrying in %d s)", _tag.c_str(), stage.c_str(), what.c_str(), backoff_s);
            if (!_wait(backoff_s))
                break;
            backoff_s = min(backoff_s * 2, 600);
        }
    }

    _apply(_tracker.close_all());
    lock_guard<mutex> g(_lock);
    _status.state = "stopped";
}
