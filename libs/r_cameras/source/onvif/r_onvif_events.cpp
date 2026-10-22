#include "r_cameras/onvif/r_onvif_events.h"
#include "r_onvif/r_onvif_session.h"
#include "r_utils/r_logger.h"
#include <chrono>

using namespace r_cameras;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

static const int PULL_TIMEOUT_S = 2;       // short, so a stop is never waiting on a long pull
static const int TERMINATION_S = 120;

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

            stage = "CreatePullPointSubscription";
            auto pp = cam.create_pull_point_subscription(events.value(), TERMINATION_S);
            {
                int port = ep.value().port;
                _update([port](r_camera_events_status& s) { s.up = true; s.state = "subscribed"; s.port = port; s.last_error.clear(); });
            }
            R_LOG_INFO("%ssubscribed on port %d", _prefix().c_str(), ep.value().port);
            backoff_s = 10;

            // A camera whose motion region is empty detects nothing: give it
            // the whole frame. A region anyone drew is left alone. Once; a
            // failure here doesn't stop the events.
            if (!_region_checked)
            {
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
            // Without a subscription the camera's state is unknown: end what
            // was open (a fallback, or the next subscription's Initialized
            // state, starts it again if it still is).
            _apply(_tracker.close_all());
            string what = _first_line(e.what());
            _update([&](r_camera_events_status& s) {
                s.up = false;
                s.state = "down";
                ++s.failures;
                s.last_error = stage + ": " + what;
            });
            R_LOG_WARNING("%sfailed %s: %s (retrying in %d s)", _prefix().c_str(), stage.c_str(), what.c_str(), backoff_s);
            if (!_wait(backoff_s))
                break;
            backoff_s = min(backoff_s * 2, 600);
        }
    }

    _apply(_tracker.close_all());
}
