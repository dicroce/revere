#include "test_r_stream_probe.h"
#include "stream_probe_clips.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_stream_probe.h"
#include "r_fakey/r_fake_camera.h"
#include "r_utils/r_string_utils.h"
#include <chrono>
#include <memory>
#include <thread>
#include <cmath>
#include <utility>
#include <vector>

using namespace std;
using namespace r_pipeline;

REGISTER_TEST_FIXTURE(test_r_stream_probe);

void test_r_stream_probe::setup()
{
    gstreamer_init();
}

struct au
{
    vector<uint8_t> data;
    bool key {false};
};

// The clips' access units, split at their AUDs, each marked key when it holds an IDR (or, for H.265, any IRAP).
static vector<au> _access_units(const unsigned char* data, size_t size, bool h265)
{
    vector<au> out;
    size_t i = 0;
    while (i + 3 < size)
    {
        // A start code (3 or 4 bytes) and the NAL's type.
        size_t sc = (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) ? 3
                  : (i + 4 < size && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 0 && data[i + 3] == 1) ? 4 : 0;
        if (sc == 0) { ++i; continue; }
        uint8_t header = data[i + sc];
        int type = h265 ? ((header >> 1) & 0x3F) : (header & 0x1F);
        bool aud = h265 ? type == 35 : type == 9;
        if (aud || out.empty())
            out.push_back(au());
        if (h265 ? (type >= 16 && type <= 21) : type == 5)
            out.back().key = true;
        // Copy up to the next start code.
        size_t j = i + sc;
        while (j + 3 < size && !(data[j] == 0 && data[j + 1] == 0 && (data[j + 2] == 1 || (data[j + 2] == 0 && data[j + 3] == 1))))
            ++j;
        if (j + 3 >= size)
            j = size;
        out.back().data.insert(out.back().data.end(), data + i, data + j);
        i = j;
    }
    return out;
}

// Tallies a clip's access units at 15 fps, in decode order with rising pts.
static r_stream_probe_result _tally(const unsigned char* data, size_t size, bool h265)
{
    r_stream_tally tally(h265 ? H265_ENCODING : H264_ENCODING);
    int64_t pts = 0;
    for (auto& a : _access_units(data, size, h265))
    {
        tally.add(a.data.data(), a.data.size(), a.key, pts);
        pts += 66;
    }
    return tally.result();
}

void test_r_stream_probe::test_h264_without_b_frames()
{
    auto r = _tally(sp_h264_cb_h264, sp_h264_cb_h264_len, false);
    RTF_ASSERT_EQUAL(r.width, 64);
    RTF_ASSERT_EQUAL(r.height, 48);
    RTF_ASSERT_EQUAL(r.profile_idc, 66);
    // x264's Baseline is Constrained Baseline: constraint_set1.
    RTF_ASSERT((r.constraint_flags & 0x40) != 0);
    RTF_ASSERT(r.level_idc > 0);
    RTF_ASSERT_EQUAL(r.frames, 30);
    RTF_ASSERT_EQUAL(r.key_frames, 2);
    RTF_ASSERT(!r.b_slices);
}

void test_r_stream_probe::test_h264_b_slices()
{
    auto r = _tally(sp_h264_b_h264, sp_h264_b_h264_len, false);
    RTF_ASSERT_EQUAL(r.profile_idc, 100);
    RTF_ASSERT_EQUAL(r.frames, 30);
    // Found from the slice headers: the pts here only ever rise.
    RTF_ASSERT(r.b_slices);
}

void test_r_stream_probe::test_h265()
{
    auto none = _tally(sp_h265_b0_h265, sp_h265_b0_h265_len, true);
    RTF_ASSERT(none.encoding == H265_ENCODING);
    RTF_ASSERT_EQUAL(none.width, 64);
    RTF_ASSERT_EQUAL(none.height, 48);
    RTF_ASSERT_EQUAL(none.frames, 30);
    RTF_ASSERT_EQUAL(none.key_frames, 2);
    RTF_ASSERT(!none.b_slices);

    auto b = _tally(sp_h265_b2_h265, sp_h265_b2_h265_len, true);
    RTF_ASSERT_EQUAL(b.frames, 30);
    RTF_ASSERT(b.b_slices);
}

void test_r_stream_probe::test_pts_going_backwards()
{
    // No B-slices, but a picture whose pts is before the one decoded ahead of it.
    auto aus = _access_units(sp_h264_cb_h264, sp_h264_cb_h264_len, false);
    r_stream_tally tally(H264_ENCODING);
    for (size_t i = 0; i < aus.size(); ++i)
    {
        int64_t pts = (int64_t)i * 66;
        if (i == 5) pts = 6 * 66;
        if (i == 6) pts = 5 * 66;
        tally.add(aus[i].data.data(), aus[i].data.size(), aus[i].key, pts);
    }
    RTF_ASSERT(tally.result().b_slices);

    // The same pts in order: none.
    r_stream_tally in_order(H264_ENCODING);
    for (size_t i = 0; i < aus.size(); ++i)
        in_order.add(aus[i].data.data(), aus[i].data.size(), aus[i].key, (int64_t)i * 66);
    RTF_ASSERT(!in_order.result().b_slices);
}

void test_r_stream_probe::test_keyframe_interval_and_fps()
{
    auto r = _tally(sp_h264_cb_h264, sp_h264_cb_h264_len, false);
    // Keyframes 15 frames apart at 66 ms.
    RTF_ASSERT_EQUAL(r.key_interval_ms, (int64_t)(15 * 66));
    RTF_ASSERT(fabs(r.fps - 1000.0 / 66) < 0.01);

    // The longest gap counts: keyframes at 0, 1 s and 4.5 s.
    auto aus = _access_units(sp_h264_cb_h264, sp_h264_cb_h264_len, false);
    r_stream_tally tally(H264_ENCODING);
    tally.add(aus[0].data.data(), aus[0].data.size(), true, 0);
    tally.add(aus[1].data.data(), aus[1].data.size(), false, 500);
    tally.add(aus[15].data.data(), aus[15].data.size(), true, 1000);
    tally.add(aus[16].data.data(), aus[16].data.size(), false, 4000);
    tally.add(aus[0].data.data(), aus[0].data.size(), true, 4500);
    RTF_ASSERT_EQUAL(tally.result().key_interval_ms, (int64_t)3500);

    // One keyframe, or one frame: nothing to measure.
    r_stream_tally one(H264_ENCODING);
    one.add(aus[0].data.data(), aus[0].data.size(), true, 0);
    RTF_ASSERT_EQUAL(one.result().key_interval_ms, (int64_t)-1);
    RTF_ASSERT(one.result().fps == 0.0);

    // An access unit with no picture in it isn't a frame.
    r_stream_tally none(H264_ENCODING);
    const uint8_t aud_only[] = {0, 0, 0, 1, 0x09, 0xf0};
    none.add(aud_only, sizeof(aud_only), false, 0);
    RTF_ASSERT_EQUAL(none.result().frames, 0);
}

static vector<uint8_t> _first_sps(const unsigned char* data, size_t size)
{
    auto aus = _access_units(data, size, false);
    for (size_t i = 0; i + 4 < aus[0].data.size(); ++i)
    {
        auto& d = aus[0].data;
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && (d[i + 3] & 0x1F) == 7)
        {
            size_t j = i + 3;
            while (j + 3 < d.size() && !(d[j] == 0 && d[j + 1] == 0 && (d[j + 2] == 1 || d[j + 2] == 0)))
                ++j;
            vector<uint8_t> sps = {0, 0, 0, 1};
            sps.insert(sps.end(), d.begin() + i + 3, d.begin() + j);
            return sps;
        }
    }
    return {};
}

void test_r_stream_probe::test_sps_aspect_and_reorder()
{
    auto b = parse_h264_sps(_first_sps(sp_h264_b_h264, sp_h264_b_h264_len));
    RTF_ASSERT_EQUAL(b.width, 64);
    RTF_ASSERT_EQUAL(b.sar_num, 4);
    RTF_ASSERT_EQUAL(b.sar_den, 3);
    RTF_ASSERT(b.max_num_reorder_frames >= 1);

    auto cb = parse_h264_sps(_first_sps(sp_h264_cb_h264, sp_h264_cb_h264_len));
    // x264 writes square pixels out (1:1) when it's given no aspect.
    RTF_ASSERT_EQUAL(cb.sar_num, 1);
    RTF_ASSERT_EQUAL(cb.sar_den, 1);
    RTF_ASSERT(cb.max_num_reorder_frames <= 0);
    RTF_ASSERT((cb.constraint_flags & 0x40) != 0);
    RTF_ASSERT((b.constraint_flags & 0x40) == 0);
}

static r_stream_probe_result _good_sub()
{
    r_stream_probe_result r;
    r.answered = true;
    r.received_video = true;
    r.encoding = H264_ENCODING;
    r.width = 640;
    r.height = 480;
    r.frames = 45;
    r.key_frames = 3;
    r.fps = 15;
    r.key_interval_ms = 1000;
    return r;
}

void test_r_stream_probe::test_check_substream()
{
    RTF_ASSERT(check_substream(_good_sub(), 2560, 1920).ok);
    RTF_ASSERT(check_substream(_good_sub(), 2560, 1920).reason.empty());

    auto reason = [](r_stream_probe_result r, uint16_t w = 2560, uint16_t h = 1920) {
        auto c = check_substream(r, w, h);
        RTF_ASSERT(!c.ok);
        return c.reason;
    };
    auto r = _good_sub(); r.answered = false; r.received_video = false;
    RTF_ASSERT(reason(r) == "camera didn't answer");
    r = _good_sub(); r.received_video = false;
    RTF_ASSERT(reason(r) == "no video");
    r = _good_sub(); r.width = 0;
    RTF_ASSERT(reason(r) == "no stream size");
    // Exactly a quarter of main's pixels is allowed; a line more isn't.
    r = _good_sub(); r.width = 1280; r.height = 960;
    RTF_ASSERT(check_substream(r, 2560, 1920).ok);
    r.height = 962;
    RTF_ASSERT(reason(r) == "too large");
    r = _good_sub(); r.b_slices = true;
    RTF_ASSERT(reason(r) == "uses B-frames");
    r = _good_sub(); r.key_interval_ms = SUBSTREAM_MAX_KEY_INTERVAL_MS;
    RTF_ASSERT(check_substream(r, 2560, 1920).ok);
    r.key_interval_ms = SUBSTREAM_MAX_KEY_INTERVAL_MS + 1;
    RTF_ASSERT(reason(r) == "keyframes too far apart");
    r.key_interval_ms = -1;  // under two keyframes in the probe
    RTF_ASSERT(reason(r) == "keyframes too far apart");
    r = _good_sub(); r.fps = SUBSTREAM_MIN_FPS;
    RTF_ASSERT(check_substream(r, 2560, 1920).ok);
    r.fps = 4.9;
    RTF_ASSERT(reason(r) == "frame rate too low");
    // The first failing reason only.
    r = _good_sub(); r.b_slices = true; r.fps = 1;
    RTF_ASSERT(reason(r) == "uses B-frames");
}

void test_r_stream_probe::test_anamorphic()
{
    // A 4:3 substream of a 4:3 main.
    RTF_ASSERT(!check_substream(_good_sub(), 2560, 1920).anamorphic);
    // A 640x480 substream of a 16:9 main: squeezed, and still usable.
    auto c = check_substream(_good_sub(), 2560, 1440);
    RTF_ASSERT(c.anamorphic);
    RTF_ASSERT(c.ok);
    // 704x576 against 4:3 is 1.222 against 1.333: over 2% off.
    auto d1 = _good_sub(); d1.width = 704; d1.height = 576;
    RTF_ASSERT(check_substream(d1, 2560, 1920).anamorphic);
    // 640x360 of a 1920x1080 main is the same shape.
    auto wide = _good_sub(); wide.width = 640; wide.height = 360;
    RTF_ASSERT(!check_substream(wide, 1920, 1080).anamorphic);
    // Within 2%: 640x362 against 16:9.
    wide.height = 362;
    RTF_ASSERT(!check_substream(wide, 1920, 1080).anamorphic);
    // Set even when the stream is refused.
    auto big = _good_sub(); big.width = 1920; big.height = 1080;
    auto refused = check_substream(big, 2560, 1920);
    RTF_ASSERT(!refused.ok);
    RTF_ASSERT(refused.anamorphic);
}

// A live H.264 camera on loopback (r_fakey), 320x240 at 15 fps with a keyframe every 15 frames, on a port the system
// picks (the tests' fixed ports start at 5000, which macOS's AirPlay receiver holds). Null when this GStreamer has no
// x264enc, as some Windows builds don't: the tests that need it then do nothing.
static shared_ptr<r_fakey::r_fake_camera> _live_camera(int bframes)
{
    auto x264 = gst_element_factory_find("x264enc");
    if (!x264)
        return nullptr;
    gst_object_unref(x264);
    auto launch = r_utils::r_string_utils::format(
        "( videotestsrc is-live=true pattern=ball ! video/x-raw,width=320,height=240,framerate=15/1 ! "
        "x264enc key-int-max=15 bframes=%d b-adapt=false speed-preset=ultrafast ! "
        "video/x-h264,profile=%s ! rtph264pay name=pay0 pt=96 config-interval=-1 )",
        bframes, bframes ? "main" : "constrained-baseline");
    auto fc = make_shared<r_fakey::r_fake_camera>("/probe", launch, 0);
    thread([fc]() { fc->start(); }).detach();
    return fc;
}

void test_r_stream_probe::test_probe_stream()
{
    auto fc = _live_camera(0);
    if (!fc)
        return;
    auto r = probe_stream(r_utils::r_string_utils::format("rtsp://127.0.0.1:%d/probe", fc->bound_port()), {}, {}, 2, 9);
    fc->quit();

    RTF_ASSERT(r.answered);
    RTF_ASSERT(r.received_video);
    RTF_ASSERT(r.encoding == H264_ENCODING);
    RTF_ASSERT_EQUAL(r.width, 320);
    RTF_ASSERT_EQUAL(r.height, 240);
    RTF_ASSERT(r.key_frames >= 2);
    RTF_ASSERT(r.key_interval_ms >= 900 && r.key_interval_ms <= 1100);
    RTF_ASSERT(r.fps > 13 && r.fps < 17);
    RTF_ASSERT(!r.b_slices);
    RTF_ASSERT(check_substream(r, 1280, 960).ok);
}

void test_r_stream_probe::test_probe_stream_b_frames()
{
    auto fc = _live_camera(2);
    if (!fc)
        return;
    auto r = probe_stream(r_utils::r_string_utils::format("rtsp://127.0.0.1:%d/probe", fc->bound_port()), {}, {}, 2, 9);
    fc->quit();

    RTF_ASSERT(r.received_video);
    RTF_ASSERT(r.b_slices);
    RTF_ASSERT(check_substream(r, 1280, 960).reason == "uses B-frames");
}

void test_r_stream_probe::test_probe_stream_no_camera()
{
    // Nothing listens on port 1 (tcpmux, privileged): no answer, and the probe gives up by its deadline.
    int port = 1;
    auto started = chrono::steady_clock::now();
    auto r = probe_stream(r_utils::r_string_utils::format("rtsp://127.0.0.1:%d/probe", port), {}, {}, 1, 2);
    auto took = chrono::steady_clock::now() - started;

    RTF_ASSERT(!r.answered);
    RTF_ASSERT(!r.received_video);
    RTF_ASSERT(check_substream(r, 1280, 960).reason == "camera didn't answer");
    // Over TCP only: no answer means no retry over UDP.
    RTF_ASSERT(took < chrono::seconds(4));
}
