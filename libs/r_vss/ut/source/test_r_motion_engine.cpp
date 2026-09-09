
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
