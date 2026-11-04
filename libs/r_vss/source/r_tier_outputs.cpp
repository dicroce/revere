#include "r_vss/r_tier_outputs.h"
#include "r_vss/r_camera_stats.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/3rdparty/json/json.h"

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
