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

void test_r_restream_track::test_breaks()
{
    const int64_t ms = 1000000;
    r_restream_track_gate gate;
    // The first buffer starts the track.
    RTF_ASSERT(gate.breaks(1000 * ms, 40 * ms));
    // Each following on from where the last ended, give or take: no break.
    RTF_ASSERT(!gate.breaks(1040 * ms, 40 * ms));
    RTF_ASSERT(!gate.breaks(1081 * ms, 40 * ms));
    RTF_ASSERT(!gate.breaks(1120 * ms, 40 * ms));
    // A gap of more than RESTREAM_DISCONT_NS (frames dropped), and a step back: breaks.
    RTF_ASSERT(gate.breaks(1300 * ms, 40 * ms));
    RTF_ASSERT(!gate.breaks(1340 * ms, 40 * ms));
    RTF_ASSERT(gate.breaks(1000 * ms, 40 * ms));
    // No duration known: the next is measured from this one's start.
    RTF_ASSERT(!gate.breaks(1040 * ms, -1));
    RTF_ASSERT(!gate.breaks(1080 * ms, -1));
    // Measured from where the last ended, not where it started: 120 ms after a 40 ms buffer follows on.
    RTF_ASSERT(gate.breaks(2000 * ms, 40 * ms));
    RTF_ASSERT(!gate.breaks(2120 * ms, 40 * ms));
}

namespace
{
// Feeds 12 a-law buffers of 320 samples, 40 ms apart and each flagged DISCONT (as some cameras' buffers come), through
// restream_feed into rtppcmapay, as the live restream does. Returns each RTP packet's (timestamp, marker).
vector<pair<uint32_t, int>> restream_pcma(bool audio)
{
    r_pipeline::gstreamer_init();
    GError* err = nullptr;
    GstElement* pipeline = gst_parse_launch("appsrc name=src is-live=true format=time caps=audio/x-alaw,rate=8000,channels=1 ! "
                                            "rtppcmapay pt=8 ! appsink name=sink sync=false", &err);
    if(!pipeline)
        return {};
    GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");

    struct ctx_t
    {
        r_blocking_q<_frame_context> q {300};
        r_restream_track_gate gate;
        bool audio;
    } ctx;
    ctx.audio = audio;
    g_signal_connect(src, "need-data", G_CALLBACK(+[](GstElement* appsrc, guint, ctx_t* c) {
        restream_feed(appsrc, c->q, c->gate, c->audio);
    }), &ctx);

    for(int i = 0; i < 12; ++i)
    {
        vector<uint8_t> samples(320, 0xD5);
        _frame_context fc;
        fc.gst_pts = (uint64_t)i * 40 * GST_MSECOND;
        fc.gst_dts = fc.gst_pts;
        fc.key = true;
        fc.buffer = r_pipeline::r_gst_buffer(samples.data(), samples.size());
        GST_BUFFER_DURATION(fc.buffer.get()) = 40 * GST_MSECOND;
        GST_BUFFER_FLAG_SET(fc.buffer.get(), GST_BUFFER_FLAG_DISCONT);
        ctx.q.post(fc);
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    vector<pair<uint32_t, int>> out;
    while(out.size() < 12)
    {
        GstSample* sample = nullptr;
        g_signal_emit_by_name(sink, "try-pull-sample", (GstClockTime)(3 * GST_SECOND), &sample);
        if(!sample)
            break;
        GstMapInfo info;
        GstBuffer* b = gst_sample_get_buffer(sample);
        if(gst_buffer_map(b, &info, GST_MAP_READ) && info.size >= 12)
        {
            uint32_t ts = ((uint32_t)info.data[4] << 24) | ((uint32_t)info.data[5] << 16) | ((uint32_t)info.data[6] << 8) | info.data[7];
            out.push_back({ts, info.data[1] >> 7});
            gst_buffer_unmap(b, &info);
        }
        gst_sample_unref(sample);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(src);
    gst_object_unref(sink);
    gst_object_unref(pipeline);
    return out;
}
}

void test_r_restream_track::test_audio_restream_stamps_by_samples()
{
    // The audio track sends a discontinuity only where its times break, so the payloader counts samples: +320 a
    // packet at 8 kHz, and the marker only on the first.
    auto audio = restream_pcma(true);
    RTF_ASSERT(audio.size() == 12);
    for(size_t i = 1; i < audio.size(); ++i)
    {
        RTF_ASSERT(audio[i].first - audio[i - 1].first == 320);
        RTF_ASSERT(audio[i].second == 0);
    }
    RTF_ASSERT(audio[0].second == 1);

    // Passing every packet's DISCONT on (as video keeps its flags) is what doubled the clock: +640, marker on each.
    auto passed = restream_pcma(false);
    RTF_ASSERT(passed.size() == 12);
    RTF_ASSERT(passed[2].first - passed[1].first == 640);
    RTF_ASSERT(passed[2].second == 1);
}

void test_r_restream_track::test_audio_restream_drops_camera_resync()
{
    // A camera that sets the RTP marker on every packet gets every buffer flagged RESYNC by rtppcmadepay ("start of a
    // talkspurt"), and GStreamer 1.28's audio payloader takes RESYNC as a discontinuity, as it does DISCONT. The
    // restream's audio buffers carry neither, after the first.
    r_pipeline::gstreamer_init();
    GError* err = nullptr;
    GstElement* pipeline = gst_parse_launch("appsrc name=src is-live=true format=time caps=audio/x-alaw,rate=8000,channels=1 ! "
                                            "appsink name=sink sync=false", &err);
    RTF_ASSERT(pipeline != nullptr);
    GstElement* src = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");

    struct ctx_t
    {
        r_blocking_q<_frame_context> q {300};
        r_restream_track_gate gate;
    } ctx;
    g_signal_connect(src, "need-data", G_CALLBACK(+[](GstElement* appsrc, guint, ctx_t* c) {
        restream_feed(appsrc, c->q, c->gate, true);
    }), &ctx);

    for(int i = 0; i < 8; ++i)
    {
        vector<uint8_t> samples(320, 0xD5);
        _frame_context fc;
        fc.gst_pts = (uint64_t)i * 40 * GST_MSECOND;
        fc.gst_dts = fc.gst_pts;
        fc.key = true;
        fc.buffer = r_pipeline::r_gst_buffer(samples.data(), samples.size());
        GST_BUFFER_DURATION(fc.buffer.get()) = 40 * GST_MSECOND;
        GST_BUFFER_FLAG_SET(fc.buffer.get(), GST_BUFFER_FLAG_RESYNC);
        ctx.q.post(fc);
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    vector<guint> flags;
    while(flags.size() < 8)
    {
        GstSample* sample = nullptr;
        g_signal_emit_by_name(sink, "try-pull-sample", (GstClockTime)(3 * GST_SECOND), &sample);
        if(!sample)
            break;
        flags.push_back(GST_BUFFER_FLAGS(gst_sample_get_buffer(sample)));
        gst_sample_unref(sample);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(src);
    gst_object_unref(sink);
    gst_object_unref(pipeline);

    RTF_ASSERT(flags.size() == 8);
    for(size_t i = 1; i < flags.size(); ++i)
    {
        RTF_ASSERT((flags[i] & GST_BUFFER_FLAG_RESYNC) == 0);
        RTF_ASSERT((flags[i] & GST_BUFFER_FLAG_DISCONT) == 0);
    }
}
