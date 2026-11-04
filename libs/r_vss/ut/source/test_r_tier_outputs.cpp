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

namespace
{
r_detection_session _session(const string& camera_id, int64_t start, int64_t end, vector<string> classes)
{
    r_detection_session s;
    s.camera_id = camera_id;
    s.start_ms = start;
    s.end_ms = end;
    for(auto& c : classes)
        s.detections.push_back({camera_id, start + 1000, c, 0.8f, 10, 10, 100, 100});
    return s;
}
}

// Tier 2: what the pass confirms is in its detections (a cat, with a box); a
// report it doesn't confirm is kept, as the camera said it, with no box.
void test_r_tier_outputs::test_a_session_rules_on_the_reports_it_covers()
{
    vector<r_camera_report_span> pending {
        {"a", "animal", T0, T0 + 20000},          // the pass found a cat: confirmed
        {"a", "vehicle", T0 + 2000, T0 + 9000},   // a truck: confirmed
        {"a", "person", T0 + 5000, T0 + 15000},   // no person found: kept as reported
        {"a", "person", T0 - 4000, T0 + 1000},    // just before the session (a camera reports early): covered, kept
        {"a", "person", T0 - 9000, T0 - 6000},    // too early: not this session's
        {"a", "animal", T0 + 61000, T0 + 70000},  // after it: not this session's
        {"b", "animal", T0, T0 + 20000}           // another camera's
    };
    auto out = unconfirmed_reports(pending, _session("a", T0, T0 + 60000, {"cat", "truck", "bowl"}));
    RTF_ASSERT_EQUAL(out.size(), (size_t)2);
    RTF_ASSERT(out[0].class_name == "person" && out[0].start_ms == T0 + 5000 && out[0].end_ms == T0 + 15000);
    RTF_ASSERT(out[1].class_name == "person" && out[1].start_ms == T0 - 4000);
    // The covered ones are ruled on and gone; the rest wait for their own session.
    RTF_ASSERT_EQUAL(pending.size(), (size_t)3);
    RTF_ASSERT(pending[0].start_ms == T0 - 9000 && pending[1].start_ms == T0 + 61000 && pending[2].camera_id == "b");

    // A session that found nothing keeps every report it covers.
    vector<r_camera_report_span> one {{"a", "animal", T0, T0 + 20000}};
    out = unconfirmed_reports(one, _session("a", T0, T0 + 50000, {}));
    RTF_ASSERT(out.size() == 1 && out[0].class_name == "animal" && one.empty());
    // One that found a person doesn't confirm an animal.
    one = {{"a", "animal", T0, T0 + 20000}};
    out = unconfirmed_reports(one, _session("a", T0, T0 + 50000, {"person"}));
    RTF_ASSERT(out.size() == 1 && one.empty());
}

void test_r_tier_outputs::test_reports_no_session_came_for()
{
    vector<r_camera_report_span> pending {
        {"a", "animal", T0, T0 + 20000},
        {"a", "person", T0 + 30000, T0 + 40000}
    };
    RTF_ASSERT(stale_reports(pending, T0 + 20000 + CAMERA_REPORT_WAIT_MS - 1).empty());
    RTF_ASSERT_EQUAL(pending.size(), (size_t)2);
    auto out = stale_reports(pending, T0 + 20000 + CAMERA_REPORT_WAIT_MS);
    RTF_ASSERT(out.size() == 1 && out[0].class_name == "animal");
    RTF_ASSERT(pending.size() == 1 && pending[0].class_name == "person");
}
