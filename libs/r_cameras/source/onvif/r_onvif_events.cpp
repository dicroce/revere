#include "r_cameras/onvif/r_onvif_events.h"
#include "r_onvif/r_onvif_push.h"
#include "r_onvif/r_onvif_session.h"
#include "r_utils/r_logger.h"
#include <atomic>
#include <chrono>

using namespace r_cameras;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

static const int PULL_TIMEOUT_S = 2;       // short, so a stop is never waiting on a long pull
static atomic<int> g_termination_s {120};
static atomic<int> g_silence_floor_s {30};

void r_onvif_events::set_heartbeat_silence_floor_for_tests(int seconds)
{
    g_silence_floor_s = seconds;
}

void r_onvif_events::set_termination_seconds_for_tests(int seconds)
{
    g_termination_s = seconds;
}

// What a notification says, without its times or its PropertyOperation: two notifications with the same signature say
// the same thing again (a heartbeat repeats the state it already reported; a change is never the same as the one before).
static string _signature(const vector<r_onvif::onvif_event>& events)
{
    string sig;
    for (auto& e : events)
    {
        sig += e.topic + "|";
        for (auto& kv : e.source)
            sig += kv.first + "=" + kv.second + ";";
        sig += "|";
        for (auto& kv : e.data)
            sig += kv.first + "=" + kv.second + ";";
        sig += "\n";
    }
    return sig;
}

static string _first_line(const string& s)
{
    auto nl = s.find('\n');
    return nl == string::npos ? s : s.substr(0, nl);
}

r_onvif_events::r_onvif_events(const r_disco::r_camera& camera, transition_fn on_transition) :
    r_camera_events_base(camera, std::move(on_transition), "onvif")
{
}

r_onvif_events::~r_onvif_events() noexcept
{
    _join();
}

void r_onvif_events::_run()
{
    auto should_continue = [this]() { return _running(); };
    int backoff_s = 10;
    r_nullable<int> port_hint = _camera.onvif_port;

    while (_running())
    {
        string stage = "finding the device service";
        try
        {
            auto ep = r_onvif::find_device_service(_camera.ipv4.value(), _camera.xaddrs.value(), port_hint, _camera.onvif_protocol, should_continue);
            if (!_running())
                break;
            if (ep.is_null())
                throw runtime_error("no ONVIF device service answers");
            port_hint = ep.value().port;

            stage = "connecting";
            r_onvif::r_onvif_cam cam(ep.value().host, ep.value().port, ep.value().protocol, ep.value().uri,
                                     _camera.rtsp_username, _camera.rtsp_password);
            stage = "GetCapabilities";
            auto caps = cam.get_camera_capabilities();
            auto events = cam.get_events_service(caps);
            if (events.is_null())
            {
                int port = ep.value().port;
                _update([port](r_camera_events_status& s) { s.up = false; s.state = "no events"; s.port = port; });
                R_LOG_INFO("%sthe camera has no events service", _prefix().c_str());
                return;
            }

            // A camera that says it has no pull point (WSPullPointSupport false), or says "not supported" to creating one,
            // pushes its events to Revere instead.
            if (r_onvif::parse_ws_pull_point_support(caps) == 0)
                _push_preferred = true;
            r_onvif::onvif_pull_point pp;
            if (!_push_preferred)
            {
                stage = "CreatePullPointSubscription";
                try
                {
                    pp = cam.create_pull_point_subscription(events.value(), g_termination_s.load());
                }
                catch (const exception& e)
                {
                    if (!r_onvif::events_unsupported(stage, e.what()))
                        throw;
                    _push_preferred = true;
                }
            }
            if (_push_preferred)
            {
                R_LOG_INFO("%sthe camera has no pull point: using push events", _prefix().c_str());
                _push_session(cam, events.value(), caps, ep.value().port, stage, backoff_s);
                continue;
            }
            // However this ends (a stop, a failed pull or Renew, anything thrown) the camera is told to drop the pull point: it
            // keeps only a few, and a camera that is asked again gets another while the old one waits to expire.
            struct unsubscribe_on_exit
            {
                r_onvif::r_onvif_cam& cam;
                r_onvif::onvif_pull_point& pp;
                ~unsubscribe_on_exit() { try { cam.unsubscribe(pp); } catch (...) {} }
            } unsubscribe_it{cam, pp};
            {
                int port = ep.value().port;
                _update([port](r_camera_events_status& s) { s.up = true; s.state = "subscribed"; s.port = port; s.last_error.clear(); });
            }
            R_LOG_INFO("%ssubscribed on port %d", _prefix().c_str(), ep.value().port);
            backoff_s = 10;

            _check_region(cam, caps);

            auto renewed = steady_clock::now();
            while (_running())
            {
                stage = "PullMessages";
                _apply(_tracker.process(cam.pull_messages(pp, PULL_TIMEOUT_S, 100)));

                // Topics and classes are learned from the first pull's
                // Initialized states too, which usually cause no transitions.
                {
                    auto topic = _tracker.motion_topic();
                    auto classes = _tracker.classes();
                    _update([&](r_camera_events_status& s) { s.motion_topic = topic; s.classes = classes; });
                }

                // An event open far longer than anything real means its end was missed.
                if (_stale())
                    _apply(_tracker.close_all());

                if (steady_clock::now() - renewed > seconds(g_termination_s.load() / 2))
                {
                    stage = "Renew";
                    cam.renew(pp, g_termination_s.load());
                    renewed = steady_clock::now();
                }
            }
        }
        catch (const exception& e)
        {
            // Without a subscription the camera's state is unknown: end what
            // was open (a fallback, or the next subscription's Initialized
            // state, starts it again if it still is).
            _apply(_tracker.close_all());
            string what = _first_line(e.what());
            const bool not_supported = r_onvif::events_unsupported(stage, e.what());
            _update([&](r_camera_events_status& s) {
                s.up = false;
                s.state = not_supported ? "not supported" : "down";
                ++s.failures;
                s.last_error = stage + ": " + what;
            });
            // The camera said it doesn't do this (ActionNotSupported): asking every few minutes gets the same answer, so
            // it is asked again only rarely, in case a firmware update added it. Anything else (a timeout, a refused
            // login, a camera that is down) is retried as before.
            const bool unsupported = not_supported;
            const int wait_s = r_onvif::events_wait_s(backoff_s, unsupported);
            R_LOG_WARNING("%sfailed %s: %s (retrying in %d s%s)", _prefix().c_str(), stage.c_str(), what.c_str(), wait_s,
                          unsupported ? ", the camera says it doesn't support it" : "");
            if (!_wait(wait_s))
                break;
            backoff_s = r_onvif::events_next_backoff_s(backoff_s);
        }
    }

    _apply(_tracker.close_all());
}

void r_onvif_events::_check_region(r_onvif::r_onvif_cam& cam, const string& caps)
{
    // A camera whose motion region is empty detects nothing: give it the whole frame. A region anyone drew is left
    // alone. Once; a failure here doesn't stop the events.
    if (_region_checked)
        return;
    _region_checked = true;
    try
    {
        for (auto& rule : cam.fill_empty_cell_motion_regions(caps))
            R_LOG_INFO("%smotion region %s was empty; set it to the whole frame", _prefix().c_str(), rule.c_str());
    }
    catch (const exception& e)
    {
        R_LOG_WARNING("%scouldn't check its motion region: %s", _prefix().c_str(), _first_line(e.what()).c_str());
    }
}

void r_onvif_events::_push_session(r_onvif::r_onvif_cam& cam, const string& events_url, const string& caps, int port, string& stage, int& backoff_s)
{
    static const size_t MAX_QUEUED = 100;           // Notify bodies waiting to be parsed: a camera that floods drops the rest
    static const int HEARTBEAT_MAX_S = 20;          // repeats this close together are a heartbeat
    static const int HEARTBEAT_REPEATS = 3;         // and only after this many repeats in a row
    static const int HINT_AFTER_S = 30;             // nothing has arrived this long after subscribing

    const string camera_ip = _camera.ipv4.value();
    if (!r_onvif::is_ipv4_literal(camera_ip))
        throw runtime_error("push events need the camera's IPv4 address");
    stage = "push listener";
    const string local_ip = r_onvif::local_ip_for(camera_ip);
    if (local_ip.empty())
        throw runtime_error("no local address reaches the camera at " + camera_ip);
    auto listener = r_onvif::r_onvif_push_listener::acquire(local_ip);

    const string id = r_onvif::new_push_id();
    {
        lock_guard<mutex> g(_notify_lock);
        _notifies.clear();
    }
    listener->add(id, camera_ip, [this](const string& body) {
        {
            lock_guard<mutex> g(_notify_lock);
            if (_notifies.size() < MAX_QUEUED)
                _notifies.push_back(body);
        }
        _notify_cv.notify_all();
    });
    // The listener forgets the id however this ends.
    struct forget { shared_ptr<r_onvif::r_onvif_push_listener> l; string id; ~forget() { l->remove(id); } } forget_it{listener, id};

    stage = "Subscribe";
    auto sub = cam.subscribe_push(events_url, listener->url(id), g_termination_s.load());
    // However this ends (a stop, a lost heartbeat, a failed Renew, anything thrown) the camera is told to drop the
    // subscription: a camera keeps only a few, and one left behind posts every event twice to the same address. A
    // camera that doesn't answer isn't waited for long. (Declared after forget_it, so it runs before the listener forgets.)
    struct unsubscribe_on_exit
    {
        r_onvif::r_onvif_cam& cam;
        r_onvif::onvif_pull_point& sub;
        ~unsubscribe_on_exit() { try { cam.unsubscribe(sub); } catch (...) {} }
    } unsubscribe_it{cam, sub};
    _update([port](r_camera_events_status& s) { s.up = true; s.state = "subscribed"; s.port = port; s.last_error.clear(); });
    backoff_s = 10;
    R_LOG_INFO("%ssubscribed for push events on port %d (notifications to %s:%d)", _prefix().c_str(), port, local_ip.c_str(), listener->port());

    _check_region(cam, caps);

    // Renewed at half of what the camera granted, if it says (one gives 60 s however long is asked for), and never later
    // than half of what was asked.
    int renew_after_s = g_termination_s.load() / 2;
    if (sub.granted_seconds > 0)
        renew_after_s = min(renew_after_s, max(1, sub.granted_seconds / 2));

    auto renewed = steady_clock::now();
    const auto subscribed = renewed;
    auto last_notify = renewed;
    // A camera that sends a heartbeat (the same notification, nothing changed, every few seconds) is alive while it does.
    // One that has been seen to, several repeats in a row, and then goes quiet for several beats has lost the
    // subscription: subscribe again. A change is not a repeat, so a camera that only sends changes is never taken for
    // one that is quiet.
    int beat_s = 0, repeats = 0, run_gap_s = 0;
    string last_signature;
    bool received = false, hinted = false;
    auto last_bad_log = steady_clock::now() - hours(1);
    while (_running())
    {
        vector<string> bodies;
        {
            unique_lock<mutex> lk(_notify_lock);
            _notify_cv.wait_for(lk, seconds(PULL_TIMEOUT_S), [this]() { return !_notifies.empty() || !_running(); });
            bodies.swap(_notifies);
        }
        stage = "Notify";
        for (auto& body : bodies)
        {
            try
            {
                auto parsed = r_onvif::parse_notify(body);
                const string signature = _signature(parsed);
                _apply(_tracker.process(parsed));
                received = true;
                const auto now = steady_clock::now();
                const int gap_s = (int)duration_cast<seconds>(now - last_notify).count();
                if (signature.empty() || signature != last_signature)
                {
                    repeats = 0;
                    run_gap_s = 0;
                }
                else if (gap_s > HEARTBEAT_MAX_S)
                {
                    repeats = 0;
                    run_gap_s = 0;
                }
                else if (gap_s >= 1)
                {
                    run_gap_s = max(run_gap_s, gap_s);
                    if (++repeats >= HEARTBEAT_REPEATS)
                        beat_s = max(beat_s, run_gap_s);
                }
                last_signature = signature;
                last_notify = now;
            }
            catch (const exception& e)
            {
                // Untrusted input: drop it, and say so at most once a minute.
                if (steady_clock::now() - last_bad_log > minutes(1))
                {
                    last_bad_log = steady_clock::now();
                    R_LOG_WARNING("%sdropped a notification: %s", _prefix().c_str(), _first_line(e.what()).c_str());
                }
            }
        }
        if (received && hinted)
        {
            hinted = false;
            _update([](r_camera_events_status& s) { s.last_error.clear(); });
        }

        {
            auto topic = _tracker.motion_topic();
            auto classes = _tracker.classes();
            _update([&](r_camera_events_status& s) { s.motion_topic = topic; s.classes = classes; });
        }
        if (_stale())
            _apply(_tracker.close_all());

        if (!received && !hinted && steady_clock::now() - subscribed > seconds(HINT_AFTER_S))
        {
            hinted = true;
            const string hint = "no notification has reached Revere on port " + to_string(listener->port()) +
                                " yet (the camera must be able to connect to " + local_ip + "; a firewall may be blocking it)";
            _update([&](r_camera_events_status& s) { s.last_error = hint; });
            R_LOG_WARNING("%s%s", _prefix().c_str(), hint.c_str());
        }

        if (beat_s > 0 && steady_clock::now() - last_notify > seconds(max(g_silence_floor_s.load(), 4 * beat_s)))
            throw runtime_error("the camera's notifications stopped");

        if (steady_clock::now() - renewed > seconds(renew_after_s))
        {
            stage = "Renew";
            cam.renew(sub, g_termination_s.load());
            renewed = steady_clock::now();
        }
    }
}
