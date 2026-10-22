
#include "test_r_motion_engine.h"
#include "bad_guy.h"
#include "true_north.h"
#include "r_vss/r_motion_engine.h"
#include "r_vss/r_motion_event_sink.h"
#include "r_vss/r_motion_storage_sink.h"
#include "r_av/r_demuxer.h"
#include "r_pipeline/r_gst_buffer.h"
#include "r_pipeline/r_gst_source.h"
#include "r_utils/r_file.h"
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_av;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_motion_engine);

// ─── Test doubles ────────────────────────────────────────────────────────────

struct captured_event
{
    r_motion_event evt;
    std::string camera_id;
    int64_t ts;
    uint16_t width;
    uint16_t height;
    std::vector<motion_region> motion_regions;
};

class capturing_event_sink : public r_motion_event_sink
{
public:
    void post(r_motion_event evt, const std::string& camera_id, int64_t ts,
              const std::vector<uint8_t>& /*frame_data*/, uint16_t width, uint16_t height,
              const motion_region& /*bbox*/) override
    {
        std::lock_guard<std::mutex> lk(_mtx);
        _events.push_back({evt, camera_id, ts, width, height, {}});
    }

    void post(r_motion_event evt, const std::string& camera_id, int64_t ts,
              const std::vector<uint8_t>& /*frame_data*/, uint16_t width, uint16_t height,
              const motion_region& /*bbox*/,
              const std::vector<motion_region>& motion_regions) override
    {
        std::lock_guard<std::mutex> lk(_mtx);
        _events.push_back({evt, camera_id, ts, width, height, motion_regions});
    }

    std::vector<captured_event> events()
    {
        std::lock_guard<std::mutex> lk(_mtx);
        return _events;
    }

private:
    std::mutex _mtx;
    std::vector<captured_event> _events;
};

class null_storage_sink : public r_motion_storage_sink
{
public:
    void write(const system_clock::time_point& /*tp*/, const uint8_t* /*p*/) override {}
    void write_range(const system_clock::time_point& /*start*/,
                     const system_clock::time_point& /*end*/,
                     const uint8_t* /*p*/) override {}
};

// ─── Tests ───────────────────────────────────────────────────────────────────

void test_r_motion_engine::test_event_sink_interface()
{
    capturing_event_sink sink;

    motion_region bbox{10, 20, 100, 80, true};
    std::vector<uint8_t> frame(640 * 640 * 3, 0);

    sink.post(motion_event_start,  "cam1", 1000, frame, 640, 640, bbox);
    sink.post(motion_event_update, "cam1", 2000, frame, 640, 640, bbox);
    sink.post(motion_event_end,    "cam1", 3000, frame, 640, 640, bbox);

    auto events = sink.events();
    RTF_ASSERT_EQUAL(events.size(), (size_t)3);

    RTF_ASSERT_EQUAL(events[0].evt,       motion_event_start);
    RTF_ASSERT_EQUAL(events[0].camera_id, std::string("cam1"));
    RTF_ASSERT_EQUAL(events[0].ts,        (int64_t)1000);

    RTF_ASSERT_EQUAL(events[1].evt, motion_event_update);
    RTF_ASSERT_EQUAL(events[1].ts,  (int64_t)2000);

    RTF_ASSERT_EQUAL(events[2].evt, motion_event_end);
    RTF_ASSERT_EQUAL(events[2].ts,  (int64_t)3000);
}

void test_r_motion_engine::test_motion_component_filtering()
{
    const motion_region union_bbox{10, 10, 100, 20, true};
    const std::vector<motion_region> components{
        {10, 10, 10, 20, true},
        {100, 10, 10, 20, true}
    };

    // Both real components admit a recognition center.
    RTF_ASSERT(motion_evidence_contains_point(union_bbox, components, 15.0f, 20.0f));
    RTF_ASSERT(motion_evidence_contains_point(union_bbox, components, 105.0f, 20.0f));

    // The gap is inside the legacy union bbox but not on top of actual motion.
    RTF_ASSERT(!motion_evidence_contains_point(union_bbox, components, 60.0f, 20.0f));

    // Old motion producers still use their union box, and endpoint frames with
    // no current spatial evidence retain the legacy accept-all behavior.
    RTF_ASSERT(motion_evidence_contains_point(union_bbox, {}, 60.0f, 20.0f));
    RTF_ASSERT(motion_evidence_contains_point({0, 0, 0, 0, false}, {}, 600.0f, 600.0f));
    RTF_ASSERT(has_valid_motion_evidence(union_bbox, components));
    RTF_ASSERT(!has_valid_motion_evidence({0, 0, 0, 0, false}, {}));

    // Margin is applied to each component independently, not to the union gap.
    RTF_ASSERT(motion_evidence_contains_point(union_bbox, components, 25.0f, 20.0f, 5.0f));
    RTF_ASSERT(!motion_evidence_contains_point(union_bbox, components, 60.0f, 20.0f, 5.0f));

    capturing_event_sink sink;
    std::vector<uint8_t> frame(640 * 640 * 3, 0);
    r_motion_event_sink& sink_interface = sink;
    sink_interface.post(motion_event_start, "cam1", 1000, frame, 640, 640,
                        union_bbox, components);
    const auto events = sink.events();
    RTF_ASSERT_EQUAL(events.size(), (size_t)1);
    RTF_ASSERT_EQUAL(events[0].motion_regions.size(), (size_t)2);
}

void test_r_motion_engine::test_null_storage_sink_interface()
{
    null_storage_sink sink;

    auto now = system_clock::now();
    uint8_t flag = 1;

    // Verify neither call crashes
    RTF_ASSERT_NO_THROW(sink.write(now, &flag));
    RTF_ASSERT_NO_THROW(sink.write_range(now, now + seconds(5), &flag));
}

void test_r_motion_engine::test_engine_lifecycle()
{
    capturing_event_sink event_sink;

    auto factory = [](const r_motion_work_item& item) -> std::shared_ptr<r_motion_work_context> {
        // A factory that should never actually be called in this test (no frames posted)
        return make_shared<r_motion_work_context>(
            AV_CODEC_ID_H264,
            item.id,
            std::vector<uint8_t>{},
            make_unique<null_storage_sink>()
        );
    };

    r_motion_engine engine(factory, event_sink);
    RTF_ASSERT_NO_THROW(engine.start());
    RTF_ASSERT_NO_THROW(engine.stop());

    // No frames were posted so no events should have fired
    RTF_ASSERT(event_sink.events().empty());
}

void test_r_motion_engine::test_factory_receives_work_item()
{
    capturing_event_sink event_sink;

    std::string observed_camera_id;
    std::string observed_codec;
    bool factory_called = false;

    auto factory = [&](const r_motion_work_item& item) -> std::shared_ptr<r_motion_work_context> {
        factory_called = true;
        observed_camera_id = item.id;
        observed_codec = item.video_codec_name;
        return make_shared<r_motion_work_context>(
            AV_CODEC_ID_H264,
            item.id,
            std::vector<uint8_t>{},
            make_unique<null_storage_sink>()
        );
    };

    r_motion_engine engine(factory, event_sink);
    engine.start();

    // Non-key frames are intentionally rejected before entering the bounded
    // motion queue; they must not create work contexts or consume capacity.
    engine.post_frame(
        r_pipeline::r_gst_buffer{},
        1000,
        "h264",
        "",
        "test_camera",
        false  // not a key frame
    );

    // Give the worker thread a moment to process
    rtf_usleep(100000);

    engine.stop();

    RTF_ASSERT(!factory_called);
    RTF_ASSERT_EQUAL(engine.get_queue_size(), (size_t)0);
    RTF_ASSERT(observed_camera_id.empty());
    RTF_ASSERT(observed_codec.empty());
}

void test_r_motion_engine::test_real_frames_trigger_motion()
{
    r_pipeline::gstreamer_init();

    // Write test clips to disk so r_demuxer can open them
    r_fs::write_file(bad_guy_mp4, bad_guy_mp4_len, "bad_guy.mp4");

    capturing_event_sink event_sink;

    // Extract stream info then close the demuxer before running the engine,
    // so the file handle is released before we try to delete it on Windows.
    AVCodecID codec_id;
    vector<uint8_t> extradata;
    int vsi;
    {
        r_demuxer probe("bad_guy.mp4", false);
        vsi = probe.get_video_stream_index();
        codec_id = probe.get_stream_info(vsi).codec_id;
        extradata = probe.get_extradata(vsi);
    }

    // Factory captures codec/extradata directly — no r_devices lookup needed
    auto factory = [&](const r_motion_work_item& item) -> std::shared_ptr<r_motion_work_context> {
        return make_shared<r_motion_work_context>(
            codec_id,
            item.id,
            extradata,
            make_unique<null_storage_sink>()
        );
    };

    r_motion_engine engine(factory, event_sink);
    engine.start();

    // Feed every key frame from the clip into the engine.
    // Timestamps are synthetic milliseconds at ~30fps; the motion state machine
    // only needs them to be monotonically increasing.
    {
        r_demuxer demuxer("bad_guy.mp4", false);
        int64_t ts_ms = 0;
        while(demuxer.read_frame())
        {
            auto fi = demuxer.get_frame_info();
            if(fi.index == vsi && fi.key)
            {
                r_pipeline::r_gst_buffer buf(fi.data, fi.size);
                engine.post_frame(buf, ts_ms, "h264", "", "bad_guy_cam", true);
                ts_ms += 33; // ~30fps
            }
        }
    } // demuxer closed here — file handle released before remove_file

    // Drain: wait until the queue empties or a reasonable timeout elapses
    for(int i = 0; i < 200 && engine.get_queue_size() > 0; ++i)
        rtf_usleep(50000);

    engine.stop();

    r_fs::remove_file("bad_guy.mp4");

    // The bad_guy clip contains a person moving — we expect at least one
    // motion_event_start to have fired by the end of the clip.
    auto events = event_sink.events();
    bool has_start = false;
    for(auto& e : events)
    {
        if(e.evt == motion_event_start)
        {
            has_start = true;
            printf("  motion_event_start: camera=%s ts=%lld w=%u h=%u\n",
                   e.camera_id.c_str(), (long long)e.ts, e.width, e.height);
        }
    }
    RTF_ASSERT(has_start);
}

namespace
{

struct probe_run
{
    int start_index {-1};      // key frame whose processing posted the first start
    int64_t start_ts {-1};
    int updates_at_start {0};  // updates posted along with it (MOG2's confirmation backfill)
    std::vector<int64_t> probe_ts;
    std::vector<int64_t> frame_ts;
};

// Feeds bad_guy.mp4's key frames 1 s apart, one at a time (each processed
// before the next), with `probe` (null: none) set on the engine.
probe_run _run_clip(const std::function<bool()>& probe)
{
    r_pipeline::gstreamer_init();
    r_fs::write_file(bad_guy_mp4, bad_guy_mp4_len, "bad_guy.mp4");

    AVCodecID codec_id;
    vector<uint8_t> extradata;
    int vsi;
    {
        r_demuxer d("bad_guy.mp4", false);
        vsi = d.get_video_stream_index();
        codec_id = d.get_stream_info(vsi).codec_id;
        extradata = d.get_extradata(vsi);
    }

    probe_run run;
    capturing_event_sink sink;
    r_motion_engine engine([&](const r_motion_work_item& item) -> std::shared_ptr<r_motion_work_context> {
        return make_shared<r_motion_work_context>(codec_id, item.id, extradata, make_unique<null_storage_sink>());
    }, sink);
    if(probe)
        engine.set_probe([&](const std::string&, int64_t ts, const std::vector<uint8_t>& frame, uint16_t w, uint16_t h,
                             const motion_region&, const std::vector<motion_region>&) {
            RTF_ASSERT_EQUAL(frame.size(), (size_t)640 * 640 * 3);
            RTF_ASSERT(w == 640 && h == 640);
            run.probe_ts.push_back(ts);
            return probe();
        });
    engine.start();

    {
        r_demuxer demuxer("bad_guy.mp4", false);
        int64_t ts = 1000000;
        size_t posted = 0;
        int index = 0;
        while(demuxer.read_frame())
        {
            auto fi = demuxer.get_frame_info();
            if(fi.index != vsi || !fi.key)
                continue;
            r_pipeline::r_gst_buffer buf(fi.data, fi.size);
            engine.post_frame(buf, ts, "h264", "", "cam", true);
            ++posted;
            for(int i = 0; i < 500 && engine.processed_count() < posted; ++i)
                rtf_usleep(10000);
            run.frame_ts.push_back(ts);
            if(run.start_index < 0)
            {
                auto events = sink.events();
                for(size_t k = 0; k < events.size(); ++k)
                {
                    if(events[k].evt != motion_event_start)
                        continue;
                    run.start_index = index;
                    run.start_ts = events[k].ts;
                    for(size_t u = k + 1; u < events.size(); ++u)
                        if(events[u].evt == motion_event_update) ++run.updates_at_start;
                    break;
                }
            }
            ts += 1000;
            ++index;
        }
    }
    engine.stop();
    r_fs::remove_file("bad_guy.mp4");
    return run;
}

}

void test_r_motion_engine::test_probe_starts_event_on_first_frame()
{
    // Without a probe: MOG2's confirmation, DEFAULT_MOTION_CONFIRM_FRAMES
    // significant key frames, then a start backdated to the first of them.
    auto plain = _run_clip(nullptr);
    RTF_ASSERT(plain.start_index >= 0);
    RTF_ASSERT_EQUAL(plain.updates_at_start, (int)DEFAULT_MOTION_CONFIRM_FRAMES - 1);
    auto first_significant = plain.start_index - ((int)DEFAULT_MOTION_CONFIRM_FRAMES - 1);
    RTF_ASSERT_EQUAL(plain.start_ts, plain.frame_ts[first_significant]);

    // A probe that confirms: the event starts on that first significant
    // frame, two key frames (2 s here) sooner, at the same time stamp.
    auto confirmed = _run_clip([]{ return true; });
    RTF_ASSERT_EQUAL(confirmed.start_index, first_significant);
    RTF_ASSERT_EQUAL(confirmed.start_ts, plain.start_ts);
    RTF_ASSERT_EQUAL(confirmed.updates_at_start, 0);
    RTF_ASSERT_EQUAL(confirmed.probe_ts.size(), (size_t)1);
    RTF_ASSERT_EQUAL(confirmed.probe_ts[0], plain.start_ts);

    // A probe that doesn't: MOG2's confirmation as before, and the probe ran
    // no more than once per PROBE_INTERVAL_MS, only outside an event.
    auto refused = _run_clip([]{ return false; });
    RTF_ASSERT_EQUAL(refused.start_index, plain.start_index);
    RTF_ASSERT_EQUAL(refused.start_ts, plain.start_ts);
    RTF_ASSERT(!refused.probe_ts.empty());
    RTF_ASSERT_EQUAL(refused.probe_ts[0], plain.start_ts);
    for(size_t i = 1; i < refused.probe_ts.size(); ++i)
        RTF_ASSERT(refused.probe_ts[i] - refused.probe_ts[i - 1] >= (int64_t)r_motion_engine::PROBE_INTERVAL_MS);

    // Which frames get one: significant, not yet confirmed, and spaced.
    typedef r_motion_engine M;
    RTF_ASSERT(M::probe_due(false, true, -1, 5000));
    RTF_ASSERT(!M::probe_due(false, false, -1, 5000));
    RTF_ASSERT(!M::probe_due(true, true, -1, 5000));
    RTF_ASSERT(!M::probe_due(false, true, 4000, 4000 + M::PROBE_INTERVAL_MS - 1));
    RTF_ASSERT(M::probe_due(false, true, 4000, 4000 + M::PROBE_INTERVAL_MS));
}

void test_r_motion_engine::test_skips_to_newest_key_frame()
{
    capturing_event_sink event_sink;

    // The first frame holds the engine up for 800 ms (a slow decode); the
    // factory then throws, so no frame needs decoding. Which frames reach it
    // is what's checked.
    std::mutex mtx;
    std::vector<int64_t> seen;
    auto factory = [&](const r_motion_work_item& item) -> std::shared_ptr<r_motion_work_context> {
        bool first;
        {
            std::lock_guard<std::mutex> g(mtx);
            first = seen.empty();
            seen.push_back(item.ts);
        }
        if(first)
            rtf_usleep(800000);
        throw std::runtime_error("no decoder in this test");
    };

    r_motion_engine engine(factory, event_sink);
    engine.start();

    engine.post_frame(r_pipeline::r_gst_buffer{}, 1000, "h264", "", "a", true);
    rtf_usleep(100000);
    // Queued behind it: four more from "a" and one from "b".
    for(int64_t ts : {2000, 3000, 4000, 5000})
        engine.post_frame(r_pipeline::r_gst_buffer{}, ts, "h264", "", "a", true, 120);
    engine.post_frame(r_pipeline::r_gst_buffer{}, 2500, "h264", "", "b", true);

    for(int i = 0; i < 100 && engine.get_queue_size() > 0; ++i)
        rtf_usleep(20000);
    rtf_usleep(100000);
    engine.stop();

    // "a"'s three stale frames were skipped for its newest; "b"'s only one wasn't.
    std::lock_guard<std::mutex> g(mtx);
    RTF_ASSERT_EQUAL(engine.skipped_count(), (size_t)3);
    RTF_ASSERT_EQUAL(seen.size(), (size_t)3);
    RTF_ASSERT_EQUAL(seen[0], (int64_t)1000);
    RTF_ASSERT_EQUAL(seen[1], (int64_t)5000);
    RTF_ASSERT_EQUAL(seen[2], (int64_t)2500);
}
