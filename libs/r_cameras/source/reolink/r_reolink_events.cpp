#include "r_cameras/reolink/r_reolink_events.h"
#include "r_utils/r_logger.h"
#include <algorithm>

using namespace r_cameras;
using namespace r_utils;
using namespace std;

string r_reolink_state_tracker::class_name(const string& ai_type)
{
    if (ai_type == "people")
        return "person";
    if (ai_type == "dog_cat")
        return "animal";
    return ai_type;
}

vector<r_camera_transition> r_reolink_state_tracker::process(const reolink_state& state)
{
    vector<r_camera_transition> out;
    auto apply = [&](const string& what, bool active, const string& field) {
        bool was = _active.count(what) ? _active[what] : false;
        _active[what] = active;
        if (active != was)
            out.push_back({what, active, field, string()});
    };

    // Reolink's object detection runs apart from its motion detection: Driveway
    // reported a person (2026-10-20 10:10:54) while its motion state stayed 0.
    // A person, vehicle or animal is movement, so it is motion too.
    bool any_ai = false;
    for (auto& ai : state.ai)
        any_ai = any_ai || ai.second;
    apply("motion", state.motion || any_ai, state.motion ? "GetMdState" : "GetAiState");
    for (auto& ai : state.ai)
    {
        auto what = class_name(ai.first);
        if (find(_classes.begin(), _classes.end(), what) == _classes.end())
            _classes.push_back(what);
        apply(what, ai.second, "GetAiState/" + ai.first);
    }
    return out;
}

vector<r_camera_transition> r_reolink_state_tracker::close_all()
{
    vector<r_camera_transition> out;
    for (auto& a : _active)
    {
        if (a.second)
            out.push_back({a.first, false, a.first == "motion" ? string("GetMdState") : string("GetAiState"), string()});
        a.second = false;
    }
    return out;
}

vector<string> r_reolink_state_tracker::classes() const
{
    return _classes;
}

r_reolink_events::r_reolink_events(const r_disco::r_camera& camera, transition_fn on_transition) :
    r_camera_events_base(camera, std::move(on_transition), "reolink")
{
}

r_reolink_events::~r_reolink_events() noexcept
{
    _join();
}

static string _first_line(const string& s)
{
    auto nl = s.find('\n');
    return nl == string::npos ? s : s.substr(0, nl);
}

void r_reolink_events::_run()
{
    r_reolink_client client(_camera.ipv4.value(), _camera.rtsp_username, _camera.rtsp_password);
    int backoff_s = 10;

    while (_running())
    {
        string stage = "polling";
        try
        {
            auto first = client.poll();
            _update([](r_camera_events_status& s) { s.up = true; s.state = "polling"; s.port = 80; s.last_error.clear(); });
            R_LOG_INFO("%spolling the camera's API", _prefix().c_str());
            backoff_s = 10;

            auto state = first;
            while (_running())
            {
                _apply(_tracker.process(state));
                auto classes = _tracker.classes();
                _update([&](r_camera_events_status& s) { s.motion_topic = "GetMdState"; s.classes = classes; });
                if (!_wait(POLL_S))
                    break;
                state = client.poll();
            }
        }
        catch (const exception& e)
        {
            // The state is unknown until polling works again: end what was open.
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
