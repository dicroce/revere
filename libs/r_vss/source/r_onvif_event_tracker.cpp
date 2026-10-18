#include "r_vss/r_onvif_event_tracker.h"
#include <algorithm>
#include <cctype>

using namespace r_vss;
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

// In order of preference: the first is the best motion topic.
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

vector<r_onvif_transition> r_onvif_event_tracker::process(const vector<r_onvif::onvif_event>& events)
{
    vector<r_onvif_transition> out;
    for (auto& ev : events)
    {
        auto topic = normalize_topic(ev.topic);
        string value;
        auto r = _match(topic, ev, value);
        if (!r)
            continue;

        string what = r->what;
        bool carried_active = false;
        if (what == "motion")
        {
            int priority = (int)(r - _motion_rules());
            if (_motion_priority < 0 || priority < _motion_priority)
            {
                // A better motion topic: drop the state kept for the lesser one,
                // but a motion it has open carries over (it is the same motion),
                // so it isn't started twice or left without an end.
                for (auto it = _state.begin(); it != _state.end();)
                {
                    if (it->first.compare(0, 7, "motion|") == 0)
                    {
                        carried_active = carried_active || it->second;
                        _topic_of.erase(it->first);
                        it = _state.erase(it);
                    }
                    else
                        ++it;
                }
                _motion_priority = priority;
            }
            if (priority != _motion_priority)
                continue;  // the same motion on a lesser topic
        }
        else if (find(_classes.begin(), _classes.end(), what) == _classes.end())
            _classes.push_back(what);

        string source;
        for (auto& s : ev.source)
            source += (source.empty() ? "" : " ") + s.first + "=" + s.second;

        auto key = what + "|" + source;
        bool active = _truthy(value);
        auto it = _state.find(key);
        bool was_active = carried_active || (it != _state.end() && it->second);
        _state[key] = active;
        _topic_of[key] = ev.topic;

        // Initialized and Changed alike: only a difference from what is known is a transition.
        if (active != was_active)
            out.push_back({what, active, ev.topic, source});
    }
    return out;
}

vector<r_onvif_transition> r_onvif_event_tracker::close_all()
{
    vector<r_onvif_transition> out;
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
    return _motion_priority < 0 ? string() : string(_motion_rules()[_motion_priority].topic);
}

vector<string> r_onvif_event_tracker::classes() const
{
    return _classes;
}
