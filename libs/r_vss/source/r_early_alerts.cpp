#include "r_vss/r_early_alerts.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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

string r_vss::box_text(const r_box& b)
{
    char buf[96];
    snprintf(buf, sizeof(buf), "[%d,%d %d,%d] %dx%d", (int)lround(b.x1), (int)lround(b.y1), (int)lround(b.x2), (int)lround(b.y2),
             (int)lround(b.x2 - b.x1), (int)lround(b.y2 - b.y1));
    return buf;
}

bool r_vss::same_place(const string& class_name, const r_box& a, const r_box& b, float iou)
{
    if(box_iou(a, b) >= iou)
        return true;
    if(alert_group_of(class_name) != "vehicle")
        return false;
    const float aw = a.x2 - a.x1, ah = a.y2 - a.y1, bw = b.x2 - b.x1, bh = b.y2 - b.y1;
    if(aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0)
        return false;
    if(max(aw, bw) > VEHICLE_JITTER_SIZE * min(aw, bw) || max(ah, bh) > VEHICLE_JITTER_SIZE * min(ah, bh))
        return false;
    const float dx = fabs((a.x1 + a.x2) - (b.x1 + b.x2)) / 2, dy = fabs((a.y1 + a.y2) - (b.y1 + b.y2)) / 2;
    return dx <= min(VEHICLE_JITTER_PX, (aw + bw) / 4) && dy <= min(VEHICLE_JITTER_PX, (ah + bh) / 4);
}

bool r_vss::mostly_inside(const r_box& part, const r_box& whole)
{
    const float pa = (part.x2 - part.x1) * (part.y2 - part.y1), wa = (whole.x2 - whole.x1) * (whole.y2 - whole.y1);
    if(pa <= 0 || wa <= 0)
        return false;
    float ix1 = (std::max)(part.x1, whole.x1), iy1 = (std::max)(part.y1, whole.y1);
    float ix2 = (std::min)(part.x2, whole.x2), iy2 = (std::min)(part.y2, whole.y2);
    if(ix1 >= ix2 || iy1 >= iy2)
        return false;
    if((ix2 - ix1) * (iy2 - iy1) >= CONTAINED_FRACTION * pa)
        return true;
    // Within it across, and overlapping it for at least half its height: a cab whose box reaches above the truck's.
    return (ix2 - ix1) >= CONTAINED_ACROSS * (part.x2 - part.x1) && (iy2 - iy1) >= CONTAINED_DOWN * (part.y2 - part.y1);
}

bool r_vss::upper_part_of(const r_box& part, const r_box& whole)
{
    const float wh = whole.y2 - whole.y1;
    const float pa = (part.x2 - part.x1) * (part.y2 - part.y1), wa = (whole.x2 - whole.x1) * wh;
    if(pa <= 0 || wa <= 0)
        return false;
    return part.y2 <= whole.y1 + UPPER_PART_LINE * wh && pa < UPPER_PART_AREA * wa;
}

// How an anchor stands, for the log: "2 sightings, weak, active".
static string anchor_state_text(int sightings, bool strong, bool active)
{
    return to_string(sightings) + (sightings == 1 ? " sighting, " : " sightings, ") + (strong ? "strong, " : "weak, ") + (active ? "active" : "not active");
}

static string iou_text(float iou)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%.2f", iou);
    return buf;
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

void r_early_alerts::event_started(const string& camera_id, bool continues)
{
    lock_guard<mutex> g(_mtx);
    auto& ev = _events[camera_id];
    auto chain_sent = continues ? ev.classes_sent : set<string>();
    ev = event_state();
    ev.classes_sent = std::move(chain_sent);
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
    auto a = _anchored_locked(camera_id, class_name, box, ts);
    if(a.verdict == early_anchored)
        return a;

    d.verdict = early_sent;
    ev.classes_sent.insert(class_name);
    ev.sent.insert({class_name, ts});
    return d;
}

r_early_alerts::r_decision r_early_alerts::_anchored_locked(const string& camera_id, const string& class_name, const r_box& box, int64_t ts) const
{
    r_decision d;
    auto it = _anchors.find(camera_id);
    if(it == _anchors.end())
        return d;
    bool on = false;
    for(const auto& a : it->second)
    {
        if(a.class_name != class_name || !a.active(ts) || !same_place(class_name, a.box, box, ANCHOR_MATCH_IOU))
            continue;
        auto iou = box_iou(a.box, box);
        if(!on || iou > d.anchor_iou)
        {
            on = true;
            d.verdict = early_anchored;
            d.anchor_iou = iou;
            d.strong_anchor = a.strong;
        }
    }
    return d;
}

const r_early_alerts::anchor* r_early_alerts::_containing_anchor_locked(const string& camera_id, const string& class_name, const r_box& box, int64_t ts,
                                                                          const vector<r_seen>& same_frame, bool* whole_in_frame) const
{
    if(alert_group_of(class_name) != "vehicle")
        return nullptr;
    auto it = _anchors.find(camera_id);
    if(it == _anchors.end())
        return nullptr;
    for(const auto& a : it->second)
    {
        if(!a.strong || !a.active(ts) || alert_group_of(a.class_name) != "vehicle" || !mostly_inside(box, a.box))
            continue;
        // The whole vehicle in the same frame: this box is something else in front of it, unless it's an upper part
        // of the vehicle (its cab, boxed as well as the truck).
        bool whole_seen = false;
        for(const auto& s : same_frame)
            if(alert_group_of(s.class_name) == "vehicle" && same_place(a.class_name, a.box, s.box, ANCHOR_MATCH_IOU))
                whole_seen = true;
        if(!whole_seen || upper_part_of(box, a.box))
        {
            if(whole_in_frame)
                *whole_in_frame = whole_seen;
            return &a;
        }
    }
    return nullptr;
}

r_early_alerts::r_decision r_early_alerts::anchored(const string& camera_id, const string& class_name, const r_box& box, int64_t ts,
                                                    const vector<r_seen>* same_frame) const
{
    if(alert_group_of(class_name).empty())
        return r_decision();
    lock_guard<mutex> g(_mtx);
    auto d = _anchored_locked(camera_id, class_name, box, ts);
    if(d.verdict != early_anchored && same_frame)
    {
        bool whole = false;
        if(auto a = _containing_anchor_locked(camera_id, class_name, box, ts, *same_frame, &whole))
        {
            d.whole_in_frame = whole;
            d.verdict = early_anchored;
            d.anchor_iou = box_iou(a->box, box);
            d.strong_anchor = true;
            d.part_of_vehicle = true;
        }
    }
    return d;
}

bool r_early_alerts::part_of_parked_vehicle(const string& camera_id, const string& class_name, const r_box& box, int64_t ts,
                                            const vector<r_seen>& same_frame) const
{
    lock_guard<mutex> g(_mtx);
    return _containing_anchor_locked(camera_id, class_name, box, ts, same_frame) != nullptr;
}

bool r_early_alerts::is_parked_vehicle(const string& camera_id, const string& class_name, const r_box& box, int64_t ts, const vector<r_seen>& start_frame,
                                       const vector<r_seen>* same_frame) const
{
    if(alert_group_of(class_name) != "vehicle")
        return false;
    lock_guard<mutex> g(_mtx);
    auto it = _anchors.find(camera_id);
    if(it == _anchors.end())
        return false;
    for(const auto& a : it->second)
        if(a.class_name == class_name && a.active(ts) && same_place(class_name, a.box, box, ANCHOR_MATCH_IOU))
        {
            // Parked before the event began? Otherwise it arrived at (or took) the spot during it.
            for(const auto& s : start_frame)
                if(s.class_name == class_name && same_place(class_name, a.box, s.box, ANCHOR_MATCH_IOU))
                    return true;
        }
    // Only part of a parked vehicle (its cab, the rest hidden): parked if the whole vehicle was there before.
    if(same_frame)
        if(auto a = _containing_anchor_locked(camera_id, class_name, box, ts, *same_frame))
            for(const auto& s : start_frame)
                if(s.class_name == a->class_name && same_place(a->class_name, a->box, s.box, ANCHOR_MATCH_IOU))
                    return true;
    return false;
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

bool r_early_alerts::event_ended(const string& camera_id, int64_t ts, const vector<r_seen>& start_frame, const vector<r_seen>& end_frame,
                                 vector<string>* notes)
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
        bool in_end = false;
        for(const auto& e : end_frame)
            if(e.class_name == s.class_name)
            {
                in_end = true;
                best = max(best, box_iou(s.box, e.box));
            }
        // A vehicle's small box jittering in place (same_place) is a sighting too, never a strong anchor by itself.
        bool jitter = false;
        if(best < SIGHTING_IOU)
            for(const auto& e : end_frame)
                if(e.class_name == s.class_name && same_place(s.class_name, s.box, e.box, SIGHTING_IOU))
                    jitter = true;
        const bool noted = notes && !alert_group_of(s.class_name).empty();
        const string pairing = "end-frame IoU " + iou_text(best) + (best < SIGHTING_IOU ? ", a small box jittering in place" : "");
        const string seen = s.class_name + " " + box_text(s.box) + ": ";
        if(best < SIGHTING_IOU && !jitter)
        {
            if(noted)
                notes->push_back(seen + (in_end ? "end frame's best " + s.class_name + " IoU " + iou_text(best) + ", under " + iou_text(SIGHTING_IOU) + ": not learnt"
                                                : "no " + s.class_name + " in the end frame: not learnt"));
            continue;
        }

        bool strong = best >= STATIC_IOU;
        anchor* match = nullptr;
        float match_iou = 0.0f;
        for(auto& a : anchors)
        {
            if(a.class_name != s.class_name || !same_place(s.class_name, a.box, s.box, ANCHOR_MATCH_IOU))
                continue;
            auto iou = box_iou(a.box, s.box);
            if(!match || iou > match_iou)
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
            if(noted)
                notes->push_back(seen + pairing + ": anchor seen again (IoU " + iou_text(match_iou) + "): " +
                                 anchor_state_text(match->sightings, match->strong, match->active(ts)));
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
            if(noted)
                notes->push_back(seen + pairing + ": new anchor: " + anchor_state_text(a.sightings, a.strong, a.active(ts)));
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

string r_early_alerts::nearest_anchor(const string& camera_id, const string& class_name, const r_box& box, int64_t ts) const
{
    lock_guard<mutex> g(_mtx);
    const anchor* best = nullptr;
    float best_iou = -1.0f;
    auto it = _anchors.find(camera_id);
    if(it != _anchors.end())
        for(const auto& a : it->second)
        {
            if(a.class_name != class_name)
                continue;
            auto iou = box_iou(a.box, box);
            if(iou > best_iou)
            {
                best = &a;
                best_iou = iou;
            }
        }
    if(!best)
        return "no " + class_name + " anchor";
    return "nearest " + class_name + " anchor " + box_text(best->box) + ": IoU " + iou_text(best_iou) + ", " +
           anchor_state_text(best->sightings, best->strong, best->active(ts));
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
