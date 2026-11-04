#include "test_r_tier_outputs.h"
#include "r_vss/r_tier_outputs.h"
#include "r_vss/r_camera_stats.h"
#include "r_vss/r_system_plugin_host.h"
#include "r_utils/3rdparty/json/json.h"
#include <string>

using namespace r_vss;
using namespace std;
using json = nlohmann::json;

REGISTER_TEST_FIXTURE(test_r_tier_outputs);

static const int64_t T0 = 1767225600000LL;   // 2026-01-01T00:00:00Z

// The camera says "animal", not which: it stays "animal" (it used to become
// "dog"), "vehicle" stays "vehicle" (it used to become "car"). Classes no
// consumer acts on are left out.
void test_r_tier_outputs::test_camera_report_keeps_the_cameras_word()
{
    RTF_ASSERT_EQUAL(camera_report_class("person"), string("person"));
    RTF_ASSERT_EQUAL(camera_report_class("vehicle"), string("vehicle"));
    RTF_ASSERT_EQUAL(camera_report_class("animal"), string("animal"));
    RTF_ASSERT_EQUAL(camera_report_class("package"), string());
    RTF_ASSERT_EQUAL(camera_report_class("visitor"), string());
    RTF_ASSERT_EQUAL(camera_report_class("face"), string());
    RTF_ASSERT_EQUAL(camera_report_class("motion"), string());
    // The stats and the tier controller group them as their own group.
    RTF_ASSERT_EQUAL(detection_group("animal"), string("animal"));
    RTF_ASSERT_EQUAL(detection_group("vehicle"), string("vehicle"));
    RTF_ASSERT(reported_class("animal") && reported_class("vehicle") && reported_class("person"));
}

void test_r_tier_outputs::test_camera_report_has_no_box()
{
    auto d = camera_report("cam-1", "animal", T0);
    RTF_ASSERT(is_camera_report(d));
    RTF_ASSERT_EQUAL(d.source, string("camera"));
    RTF_ASSERT_EQUAL(d.camera_id, string("cam-1"));
    RTF_ASSERT_EQUAL(d.class_name, string("animal"));
    RTF_ASSERT(d.ts == T0);
    RTF_ASSERT(d.confidence == 0.0f);
    RTF_ASSERT(d.x1 == 0.0f && d.y1 == 0.0f && d.x2 == 0.0f && d.y2 == 0.0f);
    r_detection yolo {"cam-1", T0, "cat", 0.8f, 1, 2, 3, 4};
    RTF_ASSERT(!is_camera_report(yolo));
}

// A camera report is written with no confidence and no box (the whole frame at
// confidence 1 claimed a place and a certainty no one measured); a YOLO
// detection keeps both, normalised from 640x640.
void test_r_tier_outputs::test_analytics_json_leaves_out_a_reports_box()
{
    auto j = json::parse(analytics_json(T0, T0 + 5000, {camera_report("cam-1", "animal", T0)}, "camera"));
    auto& a = j["analytics"];
    RTF_ASSERT_EQUAL(a["source"].get<string>(), string("camera"));
    RTF_ASSERT_EQUAL(a["total_detections"].get<int>(), 1);
    auto& d = a["detections"][0];
    RTF_ASSERT_EQUAL(d["class_name"].get<string>(), string("animal"));
    RTF_ASSERT(d.contains("timestamp"));
    for(auto key : {"confidence", "x1", "y1", "x2", "y2"})
        RTF_ASSERT(!d.contains(key));

    r_detection yolo {"cam-1", T0 + 1000, "cat", 0.75f, 64, 128, 320, 640};
    auto k = json::parse(analytics_json(T0, T0 + 5000, {yolo}, "detection_pass"));
    auto& e = k["analytics"]["detections"][0];
    RTF_ASSERT_EQUAL(e["class_name"].get<string>(), string("cat"));
    RTF_ASSERT(e["confidence"].get<float>() == 0.75f);
    RTF_ASSERT(e["x1"].get<float>() == 0.1f && e["y1"].get<float>() == 0.2f && e["x2"].get<float>() == 0.5f && e["y2"].get<float>() == 1.0f);
}

namespace
{
struct posted { int n {0}; string class_name; float confidence {-1}, x1 {-1}, y1 {-1}, x2 {-1}, y2 {-1}; } got;
void v2_fn(r_system_plugin_handle, const char*, int64_t, const char* class_name, float confidence,
           float x1, float y1, float x2, float y2, int64_t)
{
    ++got.n;
    got.class_name = class_name;
    got.confidence = confidence;
    got.x1 = x1; got.y1 = y1; got.x2 = x2; got.y2 = y2;
}
}

// Over the plugin ABI a report keeps the camera's word, with the box and
// confidence all 0: the documented "no box".
void test_r_tier_outputs::test_plugins_get_a_report_with_no_box()
{
    auto d = camera_report("cam-1", "vehicle", T0);
    got = posted();
    post_detection_to_plugin(reinterpret_cast<r_system_plugin_handle>(&d), nullptr, v2_fn, d, T0 + 100);
    RTF_ASSERT(got.n == 1);
    RTF_ASSERT_EQUAL(got.class_name, string("vehicle"));
    RTF_ASSERT(got.confidence == 0.0f && got.x1 == 0.0f && got.y1 == 0.0f && got.x2 == 0.0f && got.y2 == 0.0f);
}
