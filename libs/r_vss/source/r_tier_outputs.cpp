#include "r_vss/r_tier_outputs.h"
#include "r_vss/r_camera_stats.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/3rdparty/json/json.h"

using namespace r_vss;
using namespace r_utils;
using namespace std;
using json = nlohmann::json;

string r_vss::camera_class_to_yolo(const string& what)
{
    if (what == "person")
        return "person";
    if (what == "vehicle")
        return "car";
    if (what == "animal")
        return "dog";
    return string();
}

bool r_vss::reported_class(const string& class_name)
{
    auto g = detection_group(class_name);
    return g == "person" || g == "vehicle" || g == "animal";
}

void r_vss::whole_frame(r_detection& d)
{
    d.x1 = 0.0f;
    d.y1 = 0.0f;
    d.x2 = 639.0f;
    d.y2 = 639.0f;
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
        dets.push_back({
            {"timestamp", _iso(d.ts)},
            {"class_name", d.class_name},
            {"confidence", d.confidence},
            {"x1", d.x1 / 640.0f},
            {"y1", d.y1 / 640.0f},
            {"x2", d.x2 / 640.0f},
            {"y2", d.y2 / 640.0f}
        });
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
