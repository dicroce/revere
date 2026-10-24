#include "test_r_event_timing.h"
#include "r_vss/r_event_timing.h"
#include "r_vss/r_motion_event_plugin_host.h"
#include "r_vss/r_system_plugin_host.h"
#include "r_vss/r_query.h"
#include "r_disco/r_devices.h"
#include "r_storage/r_storage_file.h"
#include "r_av/r_demuxer.h"
#include "r_utils/r_file.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/3rdparty/json/json.h"
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace r_vss;
using namespace std;
using json = nlohmann::json;

REGISTER_TEST_FIXTURE(test_r_event_timing);

// The test clip; bad_guy.h is compiled into test_r_motion_engine.cpp.
extern unsigned char bad_guy_mp4[];
extern unsigned int bad_guy_mp4_len;

static const char* const SNAP_TOP = "event_timing_top";
static const char* const SNAP_NTS = "./event_timing.nts";

void test_r_event_timing::setup()
{
    teardown();
}

void test_r_event_timing::teardown()
{
    std::filesystem::remove_all(SNAP_TOP);
    std::filesystem::remove(SNAP_NTS);
    for(const char* f : {"./event_timing.db", "./event_timing.db-shm", "./event_timing.db-wal"})
        std::filesystem::remove(f);
    std::filesystem::remove("bad_guy.mp4");
}

static const int64_t T0 = 1761259000000LL;

// The members spliced into an analytics object, parsed back.
static json analytics_with(const string& members)
{
    return json::parse(R"({"analytics": {"motion_start_time": "2025-10-23T22:36:40.000Z", "detections": [])" + members + "}}")["analytics"];
}

void test_r_event_timing::test_json_members()
{
    r_event_timing t;
    t.confirmed_at = T0 + 2300;
    t.ended_at = T0 + 31000;
    t.looks.push_back({T0, T0 + 2500, 1});
    t.looks.push_back({T0 + 1000, T0 + 3100, 0});
    t.looks.push_back({T0 + 29500, T0 + 31400, 2});

    auto a = analytics_with(event_timing_json_members(t, T0 + 31600));
    RTF_ASSERT(a["motion_start_time"] == "2025-10-23T22:36:40.000Z");
    RTF_ASSERT(a["confirmed_at"].get<int64_t>() == T0 + 2300);
    RTF_ASSERT(a["ended_at"].get<int64_t>() == T0 + 31000);
    RTF_ASSERT(a["written_at"].get<int64_t>() == T0 + 31600);
    RTF_ASSERT(a["looks"].size() == 3);
    RTF_ASSERT(a["looks"][0] == json({{"ts", T0}, {"inferred_at", T0 + 2500}, {"n_dets", 1}}));
    RTF_ASSERT(a["looks"][1] == json({{"ts", T0 + 1000}, {"inferred_at", T0 + 3100}, {"n_dets", 0}}));
    RTF_ASSERT(a["looks"][2] == json({{"ts", T0 + 29500}, {"inferred_at", T0 + 31400}, {"n_dets", 2}}));

    // Wall clock, epoch ms.
    auto now = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
    auto w = wall_clock_ms();
    RTF_ASSERT(w >= now && w - now < 1000);
}

void test_r_event_timing::test_unknown_times_left_out()
{
    // The plugin never saw the START (its queue was full) and ran no YOLO.
    r_event_timing t;
    t.ended_at = T0 + 31000;
    auto a = analytics_with(event_timing_json_members(t, T0 + 31600));
    RTF_ASSERT(!a.contains("confirmed_at"));
    RTF_ASSERT(!a.contains("looks"));
    RTF_ASSERT(a["ended_at"].get<int64_t>() == T0 + 31000);
    RTF_ASSERT(a["written_at"].get<int64_t>() == T0 + 31600);

    RTF_ASSERT(event_timing_json_members(r_event_timing(), 0).empty());
}

void test_r_event_timing::test_looks_capped()
{
    const auto look = [](int64_t i) { return r_yolo_look{T0 + i * 1000, T0 + i * 1000 + 80, (int)(i % 3)}; };

    // Up to the cap: every look, nothing dropped.
    r_event_timing t;
    for(int64_t i = 0; i < (int64_t)MAX_EVENT_LOOKS; ++i)
        add_look(t, look(i));
    RTF_ASSERT(t.looks.size() == MAX_EVENT_LOOKS && t.looks_dropped == 0);
    RTF_ASSERT(!analytics_with(event_timing_json_members(t, T0)).contains("looks_dropped"));

    // One more: the oldest of the recent half goes.
    add_look(t, look(MAX_EVENT_LOOKS));
    RTF_ASSERT(t.looks.size() == MAX_EVENT_LOOKS && t.looks_dropped == 1);
    const int64_t half = MAX_EVENT_LOOKS / 2;
    RTF_ASSERT(t.looks[half - 1].ts == look(half - 1).ts);
    RTF_ASSERT(t.looks[half].ts == look(half + 1).ts);
    RTF_ASSERT(t.looks.back().ts == look(MAX_EVENT_LOOKS).ts);

    // A look a second for well over an hour: the first and the latest half,
    // in order, and a record that stays small.
    r_event_timing long_event;
    const int64_t n = 5000;
    for(int64_t i = 0; i < n; ++i)
        add_look(long_event, look(i));
    RTF_ASSERT(long_event.looks.size() == MAX_EVENT_LOOKS);
    RTF_ASSERT(long_event.looks_dropped == (size_t)(n - MAX_EVENT_LOOKS));
    for(int64_t i = 0; i < half; ++i)
        RTF_ASSERT(long_event.looks[i].ts == look(i).ts);
    for(int64_t i = 0; i < half; ++i)
        RTF_ASSERT(long_event.looks[half + i].ts == look(n - half + i).ts);

    auto members = event_timing_json_members(long_event, T0 + n * 1000);
    RTF_ASSERT(members.size() < 30 * 1024);
    auto a = analytics_with(members);
    RTF_ASSERT(a["looks"].size() == MAX_EVENT_LOOKS);
    RTF_ASSERT(a["looks_dropped"].get<int64_t>() == n - (int64_t)MAX_EVENT_LOOKS);
}

namespace
{
struct received
{
    int v1 {0}, v2 {0};
    string camera_id, class_name;
    int64_t ts {0}, posted_at {0};
    float confidence {0}, x2 {0};
};
received got;

void v1_fn(r_system_plugin_handle, const char* camera_id, int64_t ts, const char* class_name,
           float confidence, float, float, float x2, float)
{
    ++got.v1;
    got.camera_id = camera_id; got.ts = ts; got.class_name = class_name; got.confidence = confidence; got.x2 = x2;
}

void v2_fn(r_system_plugin_handle, const char* camera_id, int64_t ts, const char* class_name,
           float confidence, float, float, float x2, float, int64_t posted_at_ms)
{
    ++got.v2;
    got.camera_id = camera_id; got.ts = ts; got.class_name = class_name; got.confidence = confidence; got.x2 = x2;
    got.posted_at = posted_at_ms;
}
}

void test_r_event_timing::test_post_detection_prefers_v2()
{
    r_detection det;
    det.camera_id = "cam-door";
    det.ts = T0;
    det.class_name = "person";
    det.confidence = 0.91f;
    det.x1 = 10; det.y1 = 20; det.x2 = 300; det.y2 = 400;
    auto plugin = reinterpret_cast<r_system_plugin_handle>(&det);

    // Both exported: v2 only, with posted_at.
    got = received();
    post_detection_to_plugin(plugin, v1_fn, v2_fn, det, T0 + 2600);
    RTF_ASSERT(got.v1 == 0 && got.v2 == 1);
    RTF_ASSERT(got.posted_at == T0 + 2600);
    RTF_ASSERT(got.camera_id == "cam-door" && got.class_name == "person" && got.ts == T0);
    RTF_ASSERT(got.confidence == 0.91f && got.x2 == 300);

    // Only v1 (an older plugin): v1 as before.
    got = received();
    post_detection_to_plugin(plugin, v1_fn, nullptr, det, T0 + 2600);
    RTF_ASSERT(got.v1 == 1 && got.v2 == 0);
    RTF_ASSERT(got.camera_id == "cam-door" && got.class_name == "person" && got.ts == T0 && got.x2 == 300);

    // Only v2.
    got = received();
    post_detection_to_plugin(plugin, nullptr, v2_fn, det, T0 + 2600);
    RTF_ASSERT(got.v1 == 0 && got.v2 == 1 && got.posted_at == T0 + 2600);

    // Neither, or no plugin: nothing.
    got = received();
    post_detection_to_plugin(plugin, nullptr, nullptr, det, T0 + 2600);
    post_detection_to_plugin(nullptr, v1_fn, v2_fn, det, T0 + 2600);
    RTF_ASSERT(got.v1 == 0 && got.v2 == 0);
}

// /jpg and /webp send X-Frame-Ts from the frame_ts these return: the key frame
// actually decoded. That is the key frame of the first frame at or after the
// time asked for, so usually the one before it, but the next one when the time
// falls between a key frame and the frame before it; the newest when the time
// is past every frame.
void test_r_event_timing::test_snapshot_reports_key_frame_ts()
{
    using namespace r_utils;

    // Three key frames from a real clip (Annex B, SPS/PPS inline), 2 s apart,
    // with stand-in delta frames every 500 ms between them (never decoded).
    r_fs::write_file(bad_guy_mp4, bad_guy_mp4_len, "bad_guy.mp4");
    vector<vector<uint8_t>> keys;
    {
        r_av::r_demuxer demuxer("bad_guy.mp4");
        auto vsi = demuxer.get_video_stream_index();
        while(keys.size() < 3 && demuxer.read_frame())
        {
            auto fi = demuxer.get_frame_info();
            if(fi.index == vsi && fi.key)
                keys.emplace_back(fi.data, fi.data + fi.size);
        }
    }
    RTF_ASSERT(keys.size() == 3);

    const int64_t base = wall_clock_ms() - 60000;
    r_storage::r_storage_file::allocate(SNAP_NTS, 1024 * 1024, 4);
    {
        r_storage::r_storage_file sf(SNAP_NTS);
        r_nullable<string> vp;
        vp.set_value(string(""));
        auto wc = sf.create_write_context("h264", vp, r_storage::R_STORAGE_MEDIA_TYPE_VIDEO);
        const vector<uint8_t> delta(64, 0x41);
        for(size_t i = 0; i < keys.size(); ++i)
        {
            const int64_t k = (int64_t)i * 2000;
            sf.write_frame(wc, r_storage::R_STORAGE_MEDIA_TYPE_VIDEO, keys[i].data(), keys[i].size(), true, base + k, k);
            for(int64_t d = 500; d < 2000 && i + 1 < keys.size(); d += 500)
                sf.write_frame(wc, r_storage::R_STORAGE_MEDIA_TYPE_VIDEO, delta.data(), delta.size(), false, base + k + d, k + d);
        }
    }

    r_fs::mkdir(SNAP_TOP);
    r_disco::r_devices devices(SNAP_TOP);
    devices.start();
    r_disco::r_camera cam;
    cam.id = "cam-snap";
    cam.state = "assigned";
    cam.record_file_path.set_value(SNAP_NTS);
    devices.save_camera(cam);

    const auto at = [](int64_t ms) { return r_time_utils::epoch_millis_to_tp(ms); };

    int64_t frame_ts = 0;
    auto jpg = query_get_jpg(SNAP_TOP, devices, "cam-snap", at(base + 2900), 320, 240, &frame_ts);
    RTF_ASSERT(jpg.size() > 2 && jpg[0] == 0xFF && jpg[1] == 0xD8);
    RTF_ASSERT(frame_ts == base + 2000);

    frame_ts = 0;
    jpg = query_get_jpg(SNAP_TOP, devices, "cam-snap", at(base), 320, 240, &frame_ts);
    RTF_ASSERT(frame_ts == base);

    // Between the last delta frame and the next key frame: that later key frame.
    frame_ts = 0;
    jpg = query_get_jpg(SNAP_TOP, devices, "cam-snap", at(base + 3700), 320, 240, &frame_ts);
    RTF_ASSERT(frame_ts == base + 4000);

    // Past the newest frame: the newest key frame.
    frame_ts = 0;
    auto webp = query_get_webp(SNAP_TOP, devices, "cam-snap", at(base + 30000), 320, 240, &frame_ts);
    RTF_ASSERT(webp.size() > 12 && string(webp.begin() + 8, webp.begin() + 12) == "WEBP");
    RTF_ASSERT(frame_ts == base + 4000);

    // Without frame_ts, as before.
    RTF_ASSERT(!query_get_jpg(SNAP_TOP, devices, "cam-snap", at(base + 2900), 320, 240).empty());

    devices.stop();
}
