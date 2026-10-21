#include "r_vss/r_early_alerts.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>

using namespace r_vss;
using namespace std;
using json = nlohmann::json;

float r_vss::box_iou(const r_box& a, const r_box& b)
{
    float ix1 = max(a.x1, b.x1), iy1 = max(a.y1, b.y1);
    float ix2 = min(a.x2, b.x2), iy2 = min(a.y2, b.y2);
    if(ix1 >= ix2 || iy1 >= iy2)
        return 0.0f;
    float inter = (ix2 - ix1) * (iy2 - iy1);
    float uni = (a.x2 - a.x1) * (a.y2 - a.y1) + (b.x2 - b.x1) * (b.y2 - b.y1) - inter;
    return (uni > 0.0f) ? inter / uni : 0.0f;
}

string r_vss::alert_group_of(const string& class_name)
{
    static const set<string> vehicles = {"car", "truck", "bus", "motorcycle", "motorbike", "bicycle"};
    static const set<string> animals = {"bird", "cat", "dog", "horse", "sheep", "cow", "bear", "elephant", "zebra", "giraffe"};
    if(class_name == "person") return "person";
    if(vehicles.count(class_name)) return "vehicle";
    if(animals.count(class_name)) return "animal";
    return "";
}

void r_early_alerts::event_started(const string& camera_id)
{
    lock_guard<mutex> g(_mtx);
    _events[camera_id] = event_state();
}

r_early_alerts::r_decision r_early_alerts::consider(const string& camera_id, const string& class_name, float confidence, const r_box& box, int64_t ts)
{
    r_decision d;
    auto group = alert_group_of(class_name);
    if(group.empty())
        return d;

    lock_guard<mutex> g(_mtx);
    auto& ev = _events[camera_id];
    if(ev.classes_sent.count(class_name))
    {
        d.verdict = early_already_sent;
        return d;
    }
    if(confidence < MIN_CONFIDENCE)
    {
        d.verdict = early_below_floor;
        return d;
    }
    auto it = _anchors.find(camera_id);
    if(it != _anchors.end())
    {
        for(const auto& a : it->second)
        {
            if(a.class_name != class_name || !a.active(ts))
                continue;
            auto iou = box_iou(a.box, box);
            if(iou >= ANCHOR_MATCH_IOU && iou > d.anchor_iou)
            {
                d.verdict = early_anchored;
                d.anchor_iou = iou;
                d.strong_anchor = a.strong;
            }
        }
        if(d.verdict == early_anchored)
            return d;
    }

    d.verdict = early_sent;
    ev.classes_sent.insert(class_name);
    ev.sent.insert({class_name, ts});
    return d;
}

bool r_early_alerts::sent_early(const string& camera_id, const string& class_name, int64_t ts) const
{
    lock_guard<mutex> g(_mtx);
    auto it = _events.find(camera_id);
    return it != _events.end() && it->second.sent.count({class_name, ts}) > 0;
}

int64_t r_early_alerts::look_interval_ms(const string& camera_id) const
{
    lock_guard<mutex> g(_mtx);
    auto it = _events.find(camera_id);
    return (it != _events.end() && !it->second.sent.empty()) ? SLOW_LOOK_MS : FAST_LOOK_MS;
}

bool r_early_alerts::event_ended(const string& camera_id, int64_t ts, const vector<r_seen>& start_frame, const vector<r_seen>& end_frame)
{
    lock_guard<mutex> g(_mtx);
    auto& anchors = _anchors[camera_id];
    bool changed = false;

    // Lapsed anchors go first, so they don't take a slot or match.
    auto before = anchors.size();
    anchors.erase(remove_if(anchors.begin(), anchors.end(), [&](const anchor& a){ return a.lapsed(ts); }), anchors.end());
    changed = anchors.size() != before;

    for(const auto& s : start_frame)
    {
        // Its best match of the same class in the end frame.
        float best = 0.0f;
        for(const auto& e : end_frame)
            if(e.class_name == s.class_name)
                best = max(best, box_iou(s.box, e.box));
        if(best < SIGHTING_IOU)
            continue;

        bool strong = best >= STATIC_IOU;
        anchor* match = nullptr;
        float match_iou = 0.0f;
        for(auto& a : anchors)
        {
            if(a.class_name != s.class_name)
                continue;
            auto iou = box_iou(a.box, s.box);
            if(iou >= ANCHOR_MATCH_IOU && iou > match_iou)
            {
                match = &a;
                match_iou = iou;
            }
        }
        if(match)
        {
            if(match->last_seen != ts)
                ++match->sightings;
            match->strong = match->strong || strong;
            match->box = s.box;
            match->last_seen = ts;
        }
        else
        {
            anchor a;
            a.class_name = s.class_name;
            a.box = s.box;
            a.sightings = 1;
            a.strong = strong;
            a.last_seen = ts;
            anchors.push_back(a);
        }
        changed = true;
    }

    if(anchors.size() > MAX_ANCHORS)
    {
        sort(anchors.begin(), anchors.end(), [](const anchor& a, const anchor& b){ return a.last_seen > b.last_seen; });
        anchors.resize(MAX_ANCHORS);
    }

    _events.erase(camera_id);
    return changed;
}

size_t r_early_alerts::anchor_count(const string& camera_id, int64_t now) const
{
    lock_guard<mutex> g(_mtx);
    auto it = _anchors.find(camera_id);
    if(it == _anchors.end())
        return 0;
    return (size_t)count_if(it->second.begin(), it->second.end(), [&](const anchor& a){ return a.active(now); });
}

string r_early_alerts::to_json() const
{
    lock_guard<mutex> g(_mtx);
    json j = json::object();
    for(const auto& c : _anchors)
    {
        json arr = json::array();
        for(const auto& a : c.second)
            arr.push_back({{"class", a.class_name}, {"x1", a.box.x1}, {"y1", a.box.y1}, {"x2", a.box.x2}, {"y2", a.box.y2},
                           {"sightings", a.sightings}, {"strong", a.strong}, {"last_seen", a.last_seen}});
        j[c.first] = arr;
    }
    return j.dump();
}

void r_early_alerts::from_json(const string& s)
{
    map<string, vector<anchor>> anchors;
    try
    {
        auto j = json::parse(s);
        if(!j.is_object())
            return;
        for(auto it = j.begin(); it != j.end(); ++it)
        {
            if(!it.value().is_array())
                continue;
            for(const auto& e : it.value())
            {
                try
                {
                    anchor a;
                    a.class_name = e.at("class").get<string>();
                    a.box = {e.at("x1").get<float>(), e.at("y1").get<float>(), e.at("x2").get<float>(), e.at("y2").get<float>()};
                    a.sightings = e.at("sightings").get<int>();
                    a.strong = e.at("strong").get<bool>();
                    a.last_seen = e.at("last_seen").get<int64_t>();
                    anchors[it.key()].push_back(a);
                }
                catch(...) {}
            }
        }
    }
    catch(...)
    {
        return;
    }
    lock_guard<mutex> g(_mtx);
    _anchors = std::move(anchors);
}

bool r_vss::probe_confirms(const vector<r_probe_detection>& detections, const motion_region& motion_bbox, const vector<motion_region>& motion_regions)
{
    if(!has_valid_motion_evidence(motion_bbox, motion_regions))
        return false;
    for(const auto& d : detections)
    {
        if(d.class_name != "person" || d.confidence < r_early_alerts::MIN_CONFIDENCE)
            continue;
        float cx = (d.box.x1 + d.box.x2) / 2.0f, cy = (d.box.y1 + d.box.y2) / 2.0f;
        if(motion_evidence_contains_point(motion_bbox, motion_regions, cx, cy))
            return true;
    }
    return false;
}
