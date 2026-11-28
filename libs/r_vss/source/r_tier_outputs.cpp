#include "r_vss/r_tier_outputs.h"
#include "r_vss/r_camera_stats.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/3rdparty/json/json.h"
#include <cstdio>
#include <map>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using json = nlohmann::json;

string r_vss::camera_report_class(const string& what)
{
    if (what == "person" || what == "vehicle" || what == "animal")
        return what;
    return string();
}

r_detection r_vss::camera_report(const string& camera_id, const string& class_name, int64_t ts)
{
    r_detection d;
    d.camera_id = camera_id;
    d.ts = ts;
    d.class_name = class_name;
    d.confidence = 0.0f;
    d.x1 = d.y1 = d.x2 = d.y2 = 0.0f;
    d.source = "camera";
    return d;
}

bool r_vss::reported_class(const string& class_name)
{
    auto g = detection_group(class_name);
    return g == "person" || g == "vehicle" || g == "animal";
}

static string _iso(int64_t ms)
{
    return r_time_utils::tp_to_iso_8601(r_time_utils::epoch_millis_to_tp(ms), true);
}

string r_vss::analytics_json(int64_t start_ms, int64_t end_ms, const vector<r_detection>& detections, const string& source)
{
    json dets = json::array();
    for (auto& d : detections)
    {
        json det = {
            {"timestamp", _iso(d.ts)},
            {"class_name", d.class_name}
        };
        if (!is_camera_report(d))
        {
            det["confidence"] = d.confidence;
            det["x1"] = d.x1 / 640.0f;
            det["y1"] = d.y1 / 640.0f;
            det["x2"] = d.x2 / 640.0f;
            det["y2"] = d.y2 / 640.0f;
        }
        dets.push_back(det);
    }
    json j;
    j["analytics"] = {
        {"motion_start_time", _iso(start_ms)},
        {"motion_end_time", _iso(end_ms)},
        {"detections", dets},
        {"total_detections", detections.size()},
        {"source", source}
    };
    return j.dump();
}

static r_box _box(const r_detection& d)
{
    return {d.x1, d.y1, d.x2, d.y2};
}

static vector<r_early_alerts::r_seen> _seen(const vector<r_detection>& dets)
{
    vector<r_early_alerts::r_seen> out;
    for(const auto& d : dets)
        if(!is_camera_report(d))
            out.push_back({d.class_name, _box(d)});
    return out;
}

static bool _same_detection(const r_detection& a, const r_detection& b)
{
    return a.ts == b.ts && a.class_name == b.class_name && a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2;
}

// The rest of d's frame: of `all` (whole frames, unfiltered), what has d's ts, less d itself. For telling part of a
// parked vehicle from a car in front of it, the planner's filtered lists won't do: what was already there (the
// parked vehicle) is exactly what they leave out.
static vector<r_early_alerts::r_seen> _rest_of_frame(const vector<r_detection>& all, const r_detection& d)
{
    vector<r_early_alerts::r_seen> out;
    for(const auto& o : all)
        if(o.ts == d.ts && !_same_detection(o, d) && !is_camera_report(o))
            out.push_back({o.class_name, _box(o)});
    return out;
}

static string _at(const r_detection& d)
{
    char buf[32];
    snprintf(buf, sizeof(buf), " at %.0f%%", d.confidence * 100);
    return d.class_name + " " + box_text(_box(d)) + buf;
}

vector<r_detection> r_vss::pass_alert_now(const r_early_alerts& anchors, const string& camera_id, const vector<r_detection>& found, vector<string>* notes,
                                          const vector<r_detection>* frame)
{
    vector<r_detection> out;
    for(const auto& d : found)
    {
        // A camera's own report has no box and its own class word ("vehicle"), so no anchor ever holds it.
        // Without the whole frame, no part-of-a-vehicle judgement: never on the filtered finds alone.
        auto same = frame ? _rest_of_frame(*frame, d) : vector<r_early_alerts::r_seen>();
        auto a = anchors.anchored(camera_id, d.class_name, _box(d), d.ts, (frame && !is_camera_report(d)) ? &same : nullptr);
        if(a.verdict == r_early_alerts::early_anchored)
        {
            if(notes)
            {
                char buf[128];
                if(a.part_of_vehicle && a.whole_in_frame)
                    snprintf(buf, sizeof(buf), ": held, part of a parked vehicle (upper part, whole vehicle in frame, IoU %.2f)", a.anchor_iou);
                else if(a.part_of_vehicle)
                    snprintf(buf, sizeof(buf), ": held, part of a parked vehicle (inside its anchor, IoU %.2f)", a.anchor_iou);
                else
                    snprintf(buf, sizeof(buf), ": held, on a %s static anchor (IoU %.2f)", a.strong_anchor ? "parked" : "recurring", a.anchor_iou);
                notes->push_back(_at(d) + buf);
            }
            continue;
        }
        if(notes && alert_group_of(d.class_name) == "vehicle")
            notes->push_back(_at(d) + ": sent, " + anchors.nearest_anchor(camera_id, d.class_name, _box(d), d.ts));
        out.push_back(d);
    }
    return out;
}

vector<r_detection> r_vss::pass_record(const r_early_alerts& anchors, const r_detection_session& session, vector<string>* notes)
{
    // Before the session: what its pre-roll and first frame showed.
    auto before = _seen(session.pre_roll);
    auto first = _seen(session.first_frame);
    before.insert(before.end(), first.begin(), first.end());

    // Per vehicle class: what was kept and left out, and the first few left out with why, for the log.
    struct tally { int kept {0}, parked {0}; const r_detection* first_kept {nullptr}; vector<string> left_out; };
    const size_t LEFT_OUT_NOTES = 3;
    map<string, tally> vehicles;
    vector<string> order;
    vector<r_detection> out;
    for(const auto& d : session.detections)
    {
        const bool vehicle = !is_camera_report(d) && alert_group_of(d.class_name) == "vehicle";
        if(vehicle && !vehicles.count(d.class_name))
            order.push_back(d.class_name);
        // The rest of its frame from the session's unfiltered looks (session.seen), not the kept detections.
        auto same = vehicle ? _rest_of_frame(session.seen, d) : vector<r_early_alerts::r_seen>();
        string why;
        if(vehicle && anchors.is_parked_vehicle(session.camera_id, d.class_name, _box(d), d.ts, before, session.seen.empty() ? nullptr : &same,
                                                notes ? &why : nullptr))
        {
            auto& t = vehicles[d.class_name];
            ++t.parked;
            if(notes && t.left_out.size() < LEFT_OUT_NOTES)
                t.left_out.push_back(_at(d) + ": left out, " + why);
            continue;
        }
        if(vehicle)
        {
            auto& t = vehicles[d.class_name];
            ++t.kept;
            if(!t.first_kept)
                t.first_kept = &d;
        }
        out.push_back(d);
    }

    if(notes)
        for(const auto& name : order)
        {
            const auto& t = vehicles[name];
            string line = name + ": " + to_string(t.kept) + " kept, " + to_string(t.parked) + " left out as parked";
            if(t.first_kept)
                line += "; first kept " + box_text(_box(*t.first_kept)) + ", " +
                        anchors.nearest_anchor(session.camera_id, name, _box(*t.first_kept), t.first_kept->ts);
            notes->push_back(line);
            for(const auto& l : t.left_out)
                notes->push_back(l);
        }
    return out;
}

bool r_vss::pass_learn(r_early_alerts& anchors, const r_detection_session& session, vector<string>* notes)
{
    // Never at a cut: the motion is still on, so what stands still across it (a van waiting, a person at the door)
    // may only be waiting. The chain is learnt once, when it ends.
    if(session.cut)
        return false;
    // From the chain's first frame, never a carried-on session's own (the frame at the cut).
    const auto& start = session.continues ? session.chain_first_frame : session.first_frame;
    anchors.event_started(session.camera_id);
    return anchors.event_ended(session.camera_id, session.end_ms, _seen(start), _seen(session.last_frame), notes);
}
