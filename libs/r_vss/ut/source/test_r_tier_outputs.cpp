#include "test_r_tier_outputs.h"
#include "r_vss/r_tier_outputs.h"
#include "r_vss/r_camera_stats.h"
#include "r_vss/r_system_plugin_host.h"
#include "r_utils/3rdparty/json/json.h"
#include <string>
#include <map>
#include <tuple>
#include <vector>

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

// A camera that leaves tier 1 mid MOG2 window never gets MOG2's end event: the plugin is told the window closed.
void test_r_tier_outputs::test_leaving_tier_1_closes_an_open_mog2_window()
{
    std::map<string, int64_t> starts {{"cam-1", T0}, {"cam-2", T0}};
    vector<tuple<string, bool, int64_t>> told;
    auto notify = [&](const string& id, bool started, int64_t ts) { told.push_back({id, started, ts}); };

    RTF_ASSERT(close_open_mog2_window(starts, "cam-1", T0 + 500, notify));
    RTF_ASSERT_EQUAL(told.size(), (size_t)1);
    RTF_ASSERT(told[0] == make_tuple(string("cam-1"), false, T0 + 500));
    RTF_ASSERT_EQUAL(starts.size(), (size_t)1);   // cam-2's is left alone

    // No window open: nothing to say.
    RTF_ASSERT(!close_open_mog2_window(starts, "cam-1", T0 + 900, notify));
    RTF_ASSERT_EQUAL(told.size(), (size_t)1);

    // MOG2's end for cam-1 arriving after that close (a removal): not told again.
    RTF_ASSERT(!end_mog2_window(starts, "cam-1", T0 + 300, notify));
    RTF_ASSERT_EQUAL(told.size(), (size_t)1);

    // An open window's end: told once, with when it opened, and a second end says nothing.
    auto opened = end_mog2_window(starts, "cam-2", T0 + 700, notify);
    RTF_ASSERT(opened && *opened == T0);
    RTF_ASSERT_EQUAL(told.size(), (size_t)2);
    RTF_ASSERT(told[1] == make_tuple(string("cam-2"), false, T0 + 700));
    RTF_ASSERT(starts.empty());
    RTF_ASSERT(!end_mog2_window(starts, "cam-2", T0 + 800, notify));
    RTF_ASSERT_EQUAL(told.size(), (size_t)2);
}

static r_detection _pd(const string& cls, float x, float y, float w, float h, int64_t ts, float conf = 0.9f)
{
    r_detection d;
    d.camera_id = "cats";
    d.ts = ts;
    d.class_name = cls;
    d.confidence = conf;
    d.x1 = x; d.y1 = y; d.x2 = x + w; d.y2 = y + h;
    return d;
}

static r_detection_session _ps(int64_t start, const vector<r_detection>& pre_roll, const vector<r_detection>& first,
                               const vector<r_detection>& last, const vector<r_detection>& kept)
{
    r_detection_session s;
    s.camera_id = "cats";
    s.start_ms = start;
    s.end_ms = start + 80000;
    s.pre_roll = pre_roll;
    s.first_frame = first;
    s.last_frame = last;
    s.detections = kept;
    return s;
}

// A car parked far off on a street (a 20x12 px box that YOLO puts a few px off in each frame) went out as a car
// alert in every session on tier 3, and was in the record of a cat's event on a porch.
void test_r_tier_outputs::test_pass_parked_car()
{
    const int64_t H = 60 * 60 * 1000;
    auto car = [](float dx, float dy, int64_t ts, float conf = 0.91f) { return _pd("car", 300 + dx, 200 + dy, 20, 12, ts, conf); };
    r_early_alerts anchors;

    // Nothing learnt: the car is sent, and the record keeps it.
    vector<string> notes;
    auto now = pass_alert_now(anchors, "cats", {car(0, 0, T0)}, &notes);
    RTF_ASSERT_EQUAL(now.size(), (size_t)1);
    RTF_ASSERT_EQUAL(notes.size(), (size_t)1);
    RTF_ASSERT_EQUAL(notes[0], string("car [300,200 320,212] 20x12 at 91%: sent, no car anchor"));

    // Two sessions with it sitting in their first and last frames (jittering): an anchor.
    RTF_ASSERT(pass_learn(anchors, _ps(T0, {}, {car(0, 0, T0)}, {car(7, 1, T0 + 80000)}, {})));
    RTF_ASSERT(anchors.anchor_count("cats", T0 + 80000) == 0);
    notes.clear();
    RTF_ASSERT(pass_learn(anchors, _ps(T0 + H, {}, {car(-3, 0, T0 + H)}, {car(4, -1, T0 + H + 80000)}, {}), &notes));
    RTF_ASSERT(anchors.anchor_count("cats", T0 + H + 80000) == 1);
    RTF_ASSERT_EQUAL(notes.size(), (size_t)1);
    RTF_ASSERT(notes[0].find("anchor seen again") != string::npos && notes[0].find("2 sightings, weak, active") != string::npos);

    // The cat's session: the car is held from the instant alert, the cat goes.
    const int64_t T = T0 + 2 * H;
    auto cat = _pd("cat", 200, 400, 90, 70, T + 2000, 0.86f);
    notes.clear();
    now = pass_alert_now(anchors, "cats", {car(5, 1, T + 2000), cat}, &notes);
    RTF_ASSERT_EQUAL(now.size(), (size_t)1);
    RTF_ASSERT_EQUAL(now[0].class_name, string("cat"));
    RTF_ASSERT_EQUAL(notes.size(), (size_t)1);
    RTF_ASSERT(notes[0].find("car [305,201 325,213] 20x12 at 91%: held, on a recurring static anchor (IoU ") == 0);

    // The record leaves the car out (it was in the pre-roll), keeps the cat.
    notes.clear();
    auto kept = pass_record(anchors, _ps(T, {car(1, 0, T - 3000)}, {car(-2, 1, T)}, {car(3, 0, T + 80000)},
                                         {car(5, 1, T + 2000), cat, car(-4, 0, T + 6000), car(2, 2, T + 10000)}), &notes);
    RTF_ASSERT_EQUAL(kept.size(), (size_t)1);
    RTF_ASSERT_EQUAL(kept[0].class_name, string("cat"));
    RTF_ASSERT_EQUAL(notes.size(), (size_t)1);
    RTF_ASSERT_EQUAL(notes[0], string("car: 0 kept, 3 left out as parked"));

    // Seen only in the first frame, not the pre-roll (YOLO missed it there): still parked before the session.
    kept = pass_record(anchors, _ps(T, {}, {car(0, 0, T)}, {}, {car(5, 1, T + 2000)}));
    RTF_ASSERT(kept.empty());
    // And only in the pre-roll (the first frame missed it): the same.
    kept = pass_record(anchors, _ps(T, {car(1, 0, T - 3000)}, {cat}, {}, {car(5, 1, T + 2000), cat}));
    RTF_ASSERT_EQUAL(kept.size(), (size_t)1);
    RTF_ASSERT_EQUAL(kept[0].class_name, string("cat"));

    // A car pulling into the spot during the session (not in the pre-roll or the first frame) is kept, and says why.
    notes.clear();
    kept = pass_record(anchors, _ps(T, {}, {cat}, {}, {cat, car(0, 0, T + 9000)}), &notes);
    RTF_ASSERT_EQUAL(kept.size(), (size_t)2);
    RTF_ASSERT_EQUAL(notes.size(), (size_t)1);
    RTF_ASSERT(notes[0].find("car: 1 kept, 0 left out as parked; first kept [300,200 320,212] 20x12, nearest car anchor ") == 0);

    // A car elsewhere is sent; another camera has no anchors; a camera's own report (no box) always goes.
    now = pass_alert_now(anchors, "cats", {_pd("car", 100, 300, 20, 12, T)}, nullptr);
    RTF_ASSERT_EQUAL(now.size(), (size_t)1);
    now = pass_alert_now(anchors, "porch", {car(5, 1, T)}, nullptr);
    RTF_ASSERT_EQUAL(now.size(), (size_t)1);
    now = pass_alert_now(anchors, "cats", {camera_report("cats", "vehicle", T)}, nullptr);
    RTF_ASSERT_EQUAL(now.size(), (size_t)1);
    kept = pass_record(anchors, _ps(T, {car(0, 0, T)}, {car(0, 0, T)}, {}, {camera_report("cats", "vehicle", T)}));
    RTF_ASSERT_EQUAL(kept.size(), (size_t)1);
    // A day after it was last seen, the anchor has lapsed: the car alerts again.
    now = pass_alert_now(anchors, "cats", {car(5, 1, T0 + H + 80000 + 25 * H)}, nullptr);
    RTF_ASSERT_EQUAL(now.size(), (size_t)1);
}

// A van waiting with its engine on, a person at the door: still across a cut (the motion went on past
// MAX_SESSION_MS), so the cut session's first and last frames both show them. Nothing is learnt at a cut, nor from
// the frame the next session starts at; the chain is learnt once, from its first frame to its final last frame.
void test_r_tier_outputs::test_pass_learns_once_per_chain()
{
    const int64_t M = 300000;
    auto van = [](int64_t ts) { return _pd("truck", 400, 220, 120, 80, ts); };
    auto person = [](int64_t ts) { return _pd("person", 100, 120, 40, 110, ts); };
    r_early_alerts anchors;
    auto chain_session = [](int64_t start, bool cut, bool continues, const vector<r_detection>& chain_first,
                            const vector<r_detection>& first, const vector<r_detection>& last) {
        auto s = _ps(start, first, first, last, {});
        s.end_ms = start + 300000;
        s.cut = cut;
        s.continues = continues;
        s.chain_first_frame = chain_first;
        return s;
    };

    // Twice, a chain that starts empty: the van and the person arrive and stand through two cuts, then leave.
    for (int64_t day = 0; day < 2; ++day)
    {
        const int64_t t = T0 + day * 3 * 60 * 60 * 1000;
        vector<string> notes;
        RTF_ASSERT(!pass_learn(anchors, chain_session(t, true, false, {}, {}, {van(t + M), person(t + M)}), &notes));
        RTF_ASSERT(notes.empty());
        RTF_ASSERT(!pass_learn(anchors, chain_session(t + M, true, true, {}, {van(t + M), person(t + M)}, {van(t + 2 * M), person(t + 2 * M)})));
        // The chain ends: its first frame had neither, so nothing.
        RTF_ASSERT(!pass_learn(anchors, chain_session(t + 2 * M, false, true, {}, {van(t + 2 * M), person(t + 2 * M)}, {van(t + 3 * M), person(t + 3 * M)})));
        RTF_ASSERT_EQUAL(anchors.anchor_count("cats", t + 3 * M), (size_t)0);
    }
    // So a van arriving at that spot later alerts at once.
    const int64_t later = T0 + 7 * 60 * 60 * 1000;
    RTF_ASSERT_EQUAL(pass_alert_now(anchors, "cats", {van(later)}, nullptr).size(), (size_t)1);

    // A car parked before the motion began and still there at the chain's end: learnt at the end, from the chain's
    // first frame, even though the final session's own first frame (at the cut) doesn't show it.
    auto car = [](int64_t ts) { return _pd("car", 300, 200, 20, 12, ts); };
    for (int64_t n = 0; n < 2; ++n)
    {
        const int64_t t = later + (n + 1) * 60 * 60 * 1000;
        RTF_ASSERT(!pass_learn(anchors, chain_session(t, true, false, {car(t)}, {car(t)}, {car(t + M)})));
        RTF_ASSERT(pass_learn(anchors, chain_session(t + M, false, true, {car(t)}, {}, {car(t + 2 * M)})));
    }
    const int64_t after = later + 4 * 60 * 60 * 1000;
    RTF_ASSERT_EQUAL(anchors.anchor_count("cats", after), (size_t)1);
    RTF_ASSERT(pass_alert_now(anchors, "cats", {car(after)}, nullptr).empty());
}
