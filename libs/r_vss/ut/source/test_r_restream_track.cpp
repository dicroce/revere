#include "test_r_restream_track.h"
#include "r_vss/r_restream_track.h"
#include "r_vss/r_recording_context.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_gst_buffer.h"
#include <gst/gst.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace std;
using namespace std::chrono;
using namespace r_vss;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_restream_track);

void test_r_restream_track::test_gate()
{
    auto t0 = steady_clock::now();
    r_restream_track_gate gate;

    // Never asked: not wanted.
    RTF_ASSERT(!gate.wanted(t0));

    // Waiting in need-data: wanted, however long it waits.
    gate.begin_pull(t0);
    RTF_ASSERT(gate.wanted(t0 + seconds(60)));

    // Just pushed a frame: wanted until it's been idle for the limit.
    gate.end_pull(t0 + seconds(1));
    RTF_ASSERT(gate.wanted(t0 + seconds(5)));
    RTF_ASSERT(!gate.wanted(t0 + seconds(1) + LIVE_RESTREAM_TRACK_IDLE));
    RTF_ASSERT(gate.wanted(t0 + seconds(2), seconds(2)));
    RTF_ASSERT(!gate.wanted(t0 + seconds(3), seconds(2)));
}

void test_r_restream_track::test_format_overflow()
{
    RTF_ASSERT(format_restream_overflow({}, 30).empty());

    restream_overflow quiet;
    quiet.camera = "Porch";
    quiet.session = 1;
    RTF_ASSERT(format_restream_overflow({quiet}, 30).empty());

    restream_overflow a;
    a.camera = "Front door";
    a.session = 3;
    a.audio_dropped = 470;
    restream_overflow b;
    b.camera = "Garage";
    b.session = 5;
    b.video_dropped = 12;
    b.audio_dropped = 15;
    RTF_ASSERT(format_restream_overflow({a, quiet, b}, 30) ==
               "Live restream overflow (frames dropped in the last 30 s): Front door session 3 audio 470; Garage session 5 video 12, audio 15");
}

// The source pauses for longer than need-data used to wait (3 s). The track
// must pick up again when frames come back, rather than stall for the rest of
// the session, and the pipeline must still tear down while need-data is waiting.
void test_r_restream_track::test_feed_survives_a_pause()
{
    r_pipeline::gstreamer_init();

    GError* err = nullptr;
    GstElement* pipeline = gst_parse_launch("appsrc name=src is-live=true format=time ! fakesink name=sink sync=false signal-handoffs=true", &err);
    RTF_ASSERT(pipeline != nullptr);
    GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");

    struct ctx_t
    {
        r_blocking_q<_frame_context> q {300};
        r_restream_track_gate gate;
        std::atomic<int> received {0};
    } ctx;

    g_signal_connect(src, "need-data", G_CALLBACK(+[](GstElement* appsrc, guint, ctx_t* c) {
        restream_feed(appsrc, c->q, c->gate);
    }), &ctx);
    g_signal_connect(sink, "handoff", G_CALLBACK(+[](GstElement*, GstBuffer*, GstPad*, ctx_t* c) {
        ++c->received;
    }), &ctx);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    auto post = [&](uint64_t pts_ms) {
        uint8_t bytes[16] = {};
        _frame_context fc;
        fc.gst_pts = pts_ms * 1000000;
        fc.gst_dts = fc.gst_pts;
        fc.key = true;
        fc.buffer = r_pipeline::r_gst_buffer(bytes, sizeof(bytes));
        ctx.q.post(fc);
    };
    auto wait_for = [&](int n) {
        auto until = steady_clock::now() + seconds(3);
        while(ctx.received < n && steady_clock::now() < until)
            this_thread::sleep_for(milliseconds(10));
        return ctx.received >= n;
    };

    post(0);
    RTF_ASSERT(wait_for(1));

    // Nothing for 4 s; need-data is waiting all along, so the track is wanted.
    this_thread::sleep_for(seconds(4));
    RTF_ASSERT(ctx.gate.wanted());

    post(4000);
    RTF_ASSERT(wait_for(2));
    post(4033);
    RTF_ASSERT(wait_for(3));

    // Tear down while need-data is waiting for a frame that won't come.
    this_thread::sleep_for(milliseconds(300));
    auto t0 = steady_clock::now();
    gst_element_set_state(pipeline, GST_STATE_NULL);
    RTF_ASSERT(steady_clock::now() - t0 < seconds(2));

    gst_object_unref(src);
    gst_object_unref(sink);
    gst_object_unref(pipeline);
}
