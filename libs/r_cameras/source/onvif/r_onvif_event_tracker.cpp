#include "r_cameras/onvif/r_onvif_event_tracker.h"
#include <algorithm>
#include <cctype>

using namespace r_cameras;
using namespace std;

// A topic (normalized, matched as a prefix) and the data item that carries its
// state. Seen on our cameras on 2026-10-17: Reolink (CellMotionDetector,
// MyRuleDetector), the generic Office camera (CellMotionDetector, IVA/IVB) and
// the Axis with VMD (MotionRegionDetector, MotionAlarm).
struct r_onvif_event_tracker::rule
{
    const char* topic;
    const char* data;
    const char* what;
};

// The topics cameras raise motion on: any of them active is motion.
static const r_onvif_event_tracker::rule* _motion_rules()
{
    static const r_onvif_event_tracker::rule rules[] = {
        {"RuleEngine/MotionRegionDetector", "State", "motion"},
        {"RuleEngine/CellMotionDetector", "IsMotion", "motion"},
        {"VideoSource/MotionAlarm", "State", "motion"},
        {nullptr, nullptr, nullptr}
    };
    return rules;
}

static const r_onvif_event_tracker::rule* _detection_rules()
{
    static const r_onvif_event_tracker::rule rules[] = {
        {"RuleEngine/MyRuleDetector/PeopleDetect", "State", "person"},
        {"UserAlarm/IVA/HumanShapeDetect", "State", "person"},
        {"RuleEngine/MyRuleDetector/VehicleDetect", "State", "vehicle"},
        {"VehicleAlarm/IVB/VehicleDetect", "State", "vehicle"},
        {"RuleEngine/MyRuleDetector/DogCatDetect", "State", "animal"},
        {"RuleEngine/MyRuleDetector/Package", "State", "package"},
        {"RuleEngine/MyRuleDetector/Visitor", "State", "visitor"},
        {"RuleEngine/MyRuleDetector/FaceDetect", "State", "face"},
        {nullptr, nullptr, nullptr}
    };
    return rules;
}

static bool _starts_with(const string& s, const char* prefix)
{
    return s.compare(0, char_traits<char>::length(prefix), prefix) == 0;
}

// Cameras spell "on" as true, 1 or active.
static bool _truthy(string v)
{
    transform(v.begin(), v.end(), v.begin(), [](unsigned char c){ return (char)tolower(c); });
    return v == "true" || v == "1" || v == "active" || v == "on";
}

string r_onvif_event_tracker::normalize_topic(const string& topic)
{
    string out;
    size_t i = 0;
    while (i <= topic.size())
    {
        auto slash = topic.find('/', i);
        auto seg = topic.substr(i, slash == string::npos ? string::npos : slash - i);
        auto colon = seg.find(':');
        if (colon != string::npos)
            seg = seg.substr(colon + 1);
        if (!out.empty())
            out += '/';
        out += seg;
        if (slash == string::npos)
            break;
        i = slash + 1;
    }
    return out;
}

const r_onvif_event_tracker::rule* r_onvif_event_tracker::_match(const string& topic, const r_onvif::onvif_event& ev, string& value) const
{
    auto find_data = [&](const rule& r) {
        for (auto& d : ev.data)
            if (d.first == r.data) { value = d.second; return true; }
        return false;
    };
    for (auto r = _motion_rules(); r->topic; ++r)
        if (_starts_with(topic, r->topic) && find_data(*r))
            return r;
    for (auto r = _detection_rules(); r->topic; ++r)
        if (_starts_with(topic, r->topic) && find_data(*r))
            return r;
    return nullptr;
}

vector<r_camera_transition> r_onvif_event_tracker::process(const vector<r_onvif::onvif_event>& events)
{
    vector<r_camera_transition> out;
    for (auto& ev : events)
    {
        auto topic = normalize_topic(ev.topic);
        string value;
        auto r = _match(topic, ev, value);
        if (!r)
            continue;

        string what = r->what;
        string source;
        for (auto& s : ev.source)
            source += (source.empty() ? "" : " ") + s.first + "=" + s.second;
        bool active = _truthy(value);

        if (what == "motion")
        {
            // Initialized, Changed and Deleted alike: the state each topic is in.
            _motion_state[string(r->topic) + "|" + source] = active;
            if (active)
            {
                _motion_topic = r->topic;
                _motion_raw_topic = ev.topic;
            }
            bool any = false;
            for (auto& m : _motion_state)
                any = any || m.second;
            if (any != _motion_active)
            {
                _motion_active = any;
                out.push_back({"motion", any, active ? ev.topic : _motion_raw_topic, string()});
            }
            continue;
        }

        if (find(_classes.begin(), _classes.end(), what) == _classes.end())
            _classes.push_back(what);

        auto key = what + "|" + source;
        auto it = _state.find(key);
        bool was_active = it != _state.end() && it->second;
        _state[key] = active;
        _topic_of[key] = ev.topic;

        // Initialized and Changed alike: only a difference from what is known is a transition.
        if (active != was_active)
            out.push_back({what, active, ev.topic, source});
    }
    return out;
}

vector<r_camera_transition> r_onvif_event_tracker::close_all()
{
    vector<r_camera_transition> out;
    if (_motion_active)
        out.push_back({"motion", false, _motion_raw_topic, string()});
    _motion_active = false;
    for (auto& m : _motion_state)
        m.second = false;
    for (auto& s : _state)
    {
        if (!s.second)
            continue;
        auto bar = s.first.find('|');
        out.push_back({s.first.substr(0, bar), false, _topic_of[s.first], s.first.substr(bar + 1)});
        s.second = false;
    }
    return out;
}

string r_onvif_event_tracker::motion_topic() const
{
    return _motion_topic;
}

vector<string> r_onvif_event_tracker::classes() const
{
    return _classes;
}
