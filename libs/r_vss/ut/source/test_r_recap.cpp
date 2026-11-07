#include "test_r_recap.h"
#include "r_vss/r_recap.h"
#include "r_vss/r_query.h"
#include "r_disco/r_devices.h"
#include "r_storage/r_storage_file.h"
#include "r_av/r_demuxer.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_video_decoder.h"
#include <functional>
#include "r_utils/r_file.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_exception.h"
#include <filesystem>
#include <string>
#include <vector>

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_recap);

// The test clip; bad_guy.h is compiled into test_r_motion_engine.cpp. 96x54 at 29.97 fps,
// 165 s, with 57 key frames (about 2-8 s apart).
extern unsigned char bad_guy_mp4[];
extern unsigned int bad_guy_mp4_len;

static const char* const RECAP_TOP = "recap_top";
static const char* const RECAP_NTS = "./recap.nts";
static const char* const RECAP_OUT = "recap_out.mp4";

void test_r_recap::setup()
{
    teardown();
}

void test_r_recap::teardown()
{
    std::filesystem::remove_all(RECAP_TOP);
    std::filesystem::remove(RECAP_NTS);
    std::filesystem::remove(RECAP_OUT);
    std::filesystem::remove("recap_bad_guy.mp4");
}

static vector<int64_t> every(int64_t from, int64_t step, size_t n)
{
    vector<int64_t> v;
    for(size_t i = 0; i < n; ++i)
        v.push_back(from + step * (int64_t)i);
    return v;
}

void test_r_recap::test_pick_evenly()
{
    auto v = every(0, 1, 10);
    RTF_ASSERT(recap_pick_evenly(v, 4) == vector<int64_t>({0, 3, 6, 9}));
    RTF_ASSERT(recap_pick_evenly(v, 3) == vector<int64_t>({0, 5, 9}));
    RTF_ASSERT(recap_pick_evenly(v, 1) == vector<int64_t>({0}));
    RTF_ASSERT(recap_pick_evenly(v, 0).empty());
    RTF_ASSERT(recap_pick_evenly(v, 10) == v);
    RTF_ASSERT(recap_pick_evenly(v, 50) == v);
}

void test_r_recap::test_plan()
{
    const int64_t S = 1761259000000LL;

    // Enough key frames: those in the span, evenly, at up to 12 fps.
    auto p = plan_recap(every(S, 1000, 30), S, S + 29000);
    RTF_ASSERT(!p.decode_all);
    RTF_ASSERT(p.times == every(S, 1000, 30));
    RTF_ASSERT(p.fps == 12);

    // Many: RECAP_MAX_FRAMES of them, from the first to the last.
    auto keys = every(S, 1000, 200);
    p = plan_recap(keys, S, S + 199000);
    RTF_ASSERT(p.times.size() == RECAP_MAX_FRAMES);
    RTF_ASSERT(p.times.front() == keys.front() && p.times.back() == keys.back());
    RTF_ASSERT(p.fps == 12);

    // Key frames outside the span are left out, at both ends.
    keys = every(S - 2000, 1000, 40);
    p = plan_recap(keys, S, S + 25000);
    RTF_ASSERT(p.times == every(S, 1000, 26));

    // Exactly RECAP_MIN_FRAMES is enough: 24 frames in 2 s.
    p = plan_recap(every(S, 1000, 24), S, S + 23000);
    RTF_ASSERT(!p.decode_all && p.times.size() == 24 && p.fps == 12);

    // One fewer, in a short span: every frame decoded, 24 times from the start across it.
    p = plan_recap(every(S, 1000, 23), S, S + 24000);
    RTF_ASSERT(p.decode_all);
    RTF_ASSERT(p.times == every(S, 1000, 24));
    RTF_ASSERT(p.fps == 12);

    // At most RECAP_DECODE_ALL_MAX_MS is short; longer, the key frames there are.
    p = plan_recap(every(S, 2000, 23), S, S + RECAP_DECODE_ALL_MAX_MS);
    RTF_ASSERT(p.decode_all);
    p = plan_recap(every(S, 2000, 23), S, S + RECAP_DECODE_ALL_MAX_MS + 1);
    RTF_ASSERT(!p.decode_all);
    RTF_ASSERT(p.times == every(S, 2000, 23));
    RTF_ASSERT(p.fps == 11); // 23 frames in at least 2 s

    // A few: played slowly enough to last 2 s, but no slower than 2 fps.
    p = plan_recap(every(S, 10000, 10), S, S + 120000);
    RTF_ASSERT(p.times.size() == 10 && p.fps == 5);
    p = plan_recap(every(S, 40000, 3), S, S + 120000);
    RTF_ASSERT(p.times.size() == 3 && p.fps == 2);

    // None in a long span: no times (the caller uses the key frame showing at the start).
    p = plan_recap({S - 5000}, S, S + 120000);
    RTF_ASSERT(!p.decode_all && p.times.empty() && p.fps == 2);

    // An empty or backwards span decodes nothing in full.
    RTF_ASSERT(!plan_recap({}, S, S).decode_all);
    RTF_ASSERT(!plan_recap({}, S, S - 1000).decode_all);
}

void test_r_recap::test_pad()
{
    // A 4x2 picture in the middle of an 8x6 canvas.
    vector<uint8_t> src(4 * 2 * 3 / 2);
    fill(src.begin(), src.begin() + 8, 200);
    src[8] = 50; src[9] = 51;   // U, 2x1
    src[10] = 60; src[11] = 61; // V, 2x1
    vector<uint8_t> dst(8 * 6 * 3 / 2, 0xEE);
    recap_pad_yuv420p(src.data(), 4, 2, dst.data(), 8, 6);

    auto Y = [&](int x, int y) { return dst[y * 8 + x]; };
    auto U = [&](int x, int y) { return dst[48 + y * 4 + x]; };
    auto V = [&](int x, int y) { return dst[48 + 12 + y * 4 + x]; };
    for(int y = 0; y < 6; ++y)
        for(int x = 0; x < 8; ++x)
            RTF_ASSERT(Y(x, y) == ((x >= 2 && x < 6 && y >= 2 && y < 4) ? 200 : 16));
    for(int y = 0; y < 3; ++y)
        for(int x = 0; x < 4; ++x)
        {
            const bool in = (y == 1 && (x == 1 || x == 2));
            RTF_ASSERT(U(x, y) == (in ? 50 + (x - 1) : 128));
            RTF_ASSERT(V(x, y) == (in ? 60 + (x - 1) : 128));
        }

    // A picture larger than the canvas: its middle.
    vector<uint8_t> big(8 * 6 * 3 / 2);
    for(int i = 0; i < 48; ++i)
        big[i] = (uint8_t)i;
    for(int i = 0; i < 12; ++i)
    {
        big[48 + i] = (uint8_t)(100 + i);
        big[60 + i] = (uint8_t)(150 + i);
    }
    vector<uint8_t> small(4 * 2 * 3 / 2);
    recap_pad_yuv420p(big.data(), 8, 6, small.data(), 4, 2);
    RTF_ASSERT(vector<uint8_t>(small.begin(), small.begin() + 4) == vector<uint8_t>({18, 19, 20, 21}));
    RTF_ASSERT(vector<uint8_t>(small.begin() + 4, small.begin() + 8) == vector<uint8_t>({26, 27, 28, 29}));
    RTF_ASSERT(small[8] == 105 && small[9] == 106);   // U row 1, columns 1-2
    RTF_ASSERT(small[10] == 155 && small[11] == 156); // V
}

namespace
{

// The test clip recorded, every frame at its own time from base, for camera "cam-recap". Frames
// from gap_from up to gap_to (ms into the clip) are left out, as when a camera drops off.
struct recorded
{
    int64_t base {0};
    vector<int64_t> keys;
    unique_ptr<r_disco::r_devices> devices;

    recorded(int64_t gap_from = -1, int64_t gap_to = -1)
    {
        using namespace r_utils;
        r_fs::write_file(bad_guy_mp4, bad_guy_mp4_len, "recap_bad_guy.mp4");

        base = r_time_utils::tp_to_epoch_millis(std::chrono::system_clock::now()) - 400000;
        r_storage::r_storage_file::allocate(RECAP_NTS, 2 * 1024 * 1024, 8);
        {
            r_storage::r_storage_file sf(RECAP_NTS);
            r_nullable<string> vp;
            vp.set_value(string(""));
            auto wc = sf.create_write_context("h264", vp, r_storage::R_STORAGE_MEDIA_TYPE_VIDEO);

            r_av::r_demuxer demuxer("recap_bad_guy.mp4");
            auto vsi = demuxer.get_video_stream_index();
            auto tb = demuxer.get_stream_info(vsi).time_base;
            while(demuxer.read_frame())
            {
                auto fi = demuxer.get_frame_info();
                if(fi.index != vsi)
                    continue;
                const int64_t ms = (fi.pts * 1000 * tb.num) / tb.den;
                if(ms >= gap_from && ms < gap_to)
                    continue;
                if(fi.key)
                    keys.push_back(base + ms);
                sf.write_frame(wc, r_storage::R_STORAGE_MEDIA_TYPE_VIDEO, fi.data, fi.size, fi.key, base + ms, ms);
            }
        }

        r_fs::mkdir(RECAP_TOP);
        devices = make_unique<r_disco::r_devices>(RECAP_TOP);
        devices->start();
        r_disco::r_camera cam;
        cam.id = "cam-recap";
        cam.state = "assigned";
        cam.record_file_path.set_value(RECAP_NTS);
        devices->save_camera(cam);
    }

    ~recorded() { devices->stop(); }

    recap_result recap(int64_t from_ms, int64_t to_ms)
    {
        return query_get_recap(RECAP_TOP, *devices, "cam-recap",
            r_utils::r_time_utils::epoch_millis_to_tp(base + from_ms),
            r_utils::r_time_utils::epoch_millis_to_tp(base + to_ms));
    }
};

// The recap's video stream: its size, frame rate and frame count.
struct probed
{
    uint16_t w {0}, h {0};
    AVRational fps {0, 1};
    size_t frames {0};
    bool first_key {false};
};

probed probe(const vector<uint8_t>& mp4)
{
    r_utils::r_fs::write_file(mp4.data(), mp4.size(), RECAP_OUT);
    probed p;
    r_av::r_demuxer demuxer(RECAP_OUT);
    auto vsi = demuxer.get_video_stream_index();
    auto si = demuxer.get_stream_info(vsi);
    p.w = si.resolution.first;
    p.h = si.resolution.second;
    p.fps = si.frame_rate;
    while(demuxer.read_frame())
    {
        auto fi = demuxer.get_frame_info();
        if(fi.index != vsi)
            continue;
        if(p.frames == 0)
            p.first_key = fi.key;
        ++p.frames;
    }
    return p;
}

}

void test_r_recap::test_recap_from_key_frames()
{
    recorded rec;
    RTF_ASSERT(rec.keys.size() == 57);

    // The whole clip: each of its 57 key frames, at 12 fps.
    auto r = rec.recap(0, 166000);
    RTF_ASSERT(!r.decode_all);
    RTF_ASSERT(r.frames == 57);
    RTF_ASSERT(r.fps == 12);
    RTF_ASSERT(r.mp4.size() > 8 && string(r.mp4.begin() + 4, r.mp4.begin() + 8) == "ftyp");

    auto p = probe(r.mp4);
    RTF_ASSERT(p.w == RECAP_WIDTH && p.h == RECAP_HEIGHT);
    RTF_ASSERT(p.fps.num == 12 && p.fps.den == 1);
    RTF_ASSERT(p.frames == 57);
    RTF_ASSERT(p.first_key);

    // faststart: the index comes before the pictures.
    const string bytes(r.mp4.begin(), r.mp4.end());
    RTF_ASSERT(bytes.find("moov") < bytes.find("mdat"));
}

void test_r_recap::test_recap_decoding_every_frame()
{
    recorded rec;

    // The first 20 s has 6 key frames: every frame is decoded, and 24 kept.
    size_t in_span = 0;
    for(auto k : rec.keys)
        if(k <= rec.base + 20000)
            ++in_span;
    RTF_ASSERT(in_span == 6);

    auto r = rec.recap(0, 20000);
    RTF_ASSERT(r.decode_all);
    RTF_ASSERT(r.frames == 24);
    RTF_ASSERT(r.fps == 12);

    auto p = probe(r.mp4);
    RTF_ASSERT(p.w == RECAP_WIDTH && p.h == RECAP_HEIGHT);
    RTF_ASSERT(p.frames == 24);

    // A span starting between key frames decodes from the one before it.
    r = rec.recap(3000, 13000);
    RTF_ASSERT(r.decode_all);
    RTF_ASSERT(r.frames == 24);
}

void test_r_recap::test_recap_across_a_gap()
{
    // No recording from the key frame at 5.27 s to the one at 16.05 s (three key frames gone). Over
    // the first 20 s, a time every 833 ms: the 7 before the gap each get a frame, the first frame
    // after it stands for the 13 in the gap, and the 4 after that get a frame each.
    recorded rec(5271, 16049);
    RTF_ASSERT(rec.keys.size() == 57 - 3);

    auto r = rec.recap(0, 20000);
    RTF_ASSERT(r.decode_all);
    RTF_ASSERT(r.frames == 7 + 1 + 4);
    RTF_ASSERT(probe(r.mp4).frames == 12);
}

void test_r_recap::test_no_recording()
{
    recorded rec;

    // Before the recording, short or long: nothing to make it from.
    bool not_found = false;
    try { rec.recap(-200000, -190000); }
    catch(const r_utils::r_not_found_exception&) { not_found = true; }
    RTF_ASSERT(not_found);

    // A long span with no key frame in it, whose start is long before the recording: the
    // recording's first key frame is not the one showing then.
    not_found = false;
    try { rec.recap(-390000, -200000); }
    catch(const r_utils::r_not_found_exception&) { not_found = true; }
    RTF_ASSERT(not_found);

    // An unknown camera.
    bool threw = false;
    try
    {
        query_get_recap(RECAP_TOP, *rec.devices, "nope",
            r_utils::r_time_utils::epoch_millis_to_tp(rec.base),
            r_utils::r_time_utils::epoch_millis_to_tp(rec.base + 10000));
    }
    catch(const std::exception&) { threw = true; }
    RTF_ASSERT(threw);

    // Longer than an hour, or backwards.
    threw = false;
    try { rec.recap(0, RECAP_MAX_SPAN_MS + 1); }
    catch(const std::exception&) { threw = true; }
    RTF_ASSERT(threw);
    // Backwards, though the key frame showing at its start is within it.
    threw = false;
    try { rec.recap(10000, 9000); }
    catch(const std::exception&) { threw = true; }
    RTF_ASSERT(threw);
}

namespace
{

// A recording made here: frame i of n, at fps, flat grey at luma(i), 160x90, a key frame every gop frames, for
// camera "cam-grey". What each recap frame shows says which frame it came from.
struct grey_recording
{
    int64_t base {0};
    unique_ptr<r_disco::r_devices> devices;

    grey_recording(int n, int fps, int gop, const std::function<int(int)>& luma)
    {
        using namespace r_utils;
        base = r_time_utils::tp_to_epoch_millis(std::chrono::system_clock::now()) - 400000;
        r_storage::r_storage_file::allocate(RECAP_NTS, 2 * 1024 * 1024, 4);
        {
            r_storage::r_storage_file sf(RECAP_NTS);
            r_nullable<string> vp;
            vp.set_value(string(""));
            auto wc = sf.create_write_context("h264", vp, r_storage::R_STORAGE_MEDIA_TYPE_VIDEO);

            const uint16_t W = 160, H = 90;
            r_av::r_video_encoder enc(AV_CODEC_ID_H264, 1000000, W, H, {fps, 1}, AV_PIX_FMT_YUV420P, 0, (uint16_t)gop, AV_PROFILE_H264_MAIN, 30, "veryfast");
            // The parameter sets go in front of each key frame, as a camera sends them.
            const auto sps_pps = enc.get_extradata();
            int written = 0;
            auto drain = [&](r_av::r_codec_state st, const std::function<r_av::r_codec_state()>& again) {
                while(st == r_av::R_CODEC_STATE_HAS_OUTPUT)
                {
                    auto pi = enc.get();
                    vector<uint8_t> au;
                    if(pi.key)
                        au = sps_pps;
                    au.insert(au.end(), pi.data, pi.data + pi.size);
                    const int64_t ms = (int64_t)written * 1000 / fps;
                    sf.write_frame(wc, r_storage::R_STORAGE_MEDIA_TYPE_VIDEO, au.data(), au.size(), pi.key, base + ms, ms);
                    ++written;
                    st = again();
                }
            };
            vector<uint8_t> yuv((size_t)W * H * 3 / 2);
            for(int i = 0; i < n; ++i)
            {
                memset(yuv.data(), luma(i), (size_t)W * H);
                memset(yuv.data() + (size_t)W * H, 128, (size_t)W * H / 2);
                enc.attach_buffer(yuv.data(), yuv.size(), i);
                drain(enc.encode(), [&]() { return enc.encode(); });
            }
            drain(enc.flush(), [&]() { return enc.flush(); });
            RTF_ASSERT(written == n);
        }

        r_fs::mkdir(RECAP_TOP);
        devices = make_unique<r_disco::r_devices>(RECAP_TOP);
        devices->start();
        r_disco::r_camera cam;
        cam.id = "cam-grey";
        cam.state = "assigned";
        cam.record_file_path.set_value(RECAP_NTS);
        devices->save_camera(cam);
    }

    ~grey_recording() { devices->stop(); }

    recap_result recap(int64_t from_ms, int64_t to_ms)
    {
        return query_get_recap(RECAP_TOP, *devices, "cam-grey",
            r_utils::r_time_utils::epoch_millis_to_tp(base + from_ms),
            r_utils::r_time_utils::epoch_millis_to_tp(base + to_ms));
    }
};

// The luma at the middle of each of the recap's frames, decoded.
vector<int> middle_lumas(const vector<uint8_t>& mp4)
{
    r_utils::r_fs::write_file(mp4.data(), mp4.size(), RECAP_OUT);
    r_av::r_demuxer demuxer(RECAP_OUT);
    auto vsi = demuxer.get_video_stream_index();
    r_av::r_video_decoder decoder(demuxer.get_stream_info(vsi).codec_id, r_av::r_hw_accel::none, false);
    decoder.set_extradata(demuxer.get_extradata(vsi));
    decoder.set_low_latency();
    vector<int> out;
    auto take = [&]() {
        auto yuv = decoder.get(AV_PIX_FMT_YUV420P, RECAP_WIDTH, RECAP_HEIGHT, 1);
        out.push_back((*yuv)[(size_t)(RECAP_HEIGHT / 2) * RECAP_WIDTH + RECAP_WIDTH / 2]);
    };
    while(demuxer.read_frame())
    {
        auto fi = demuxer.get_frame_info();
        if(fi.index != vsi)
            continue;
        decoder.attach_buffer(fi.data, fi.size);
        for(auto ds = decoder.decode(); ds == r_av::R_CODEC_STATE_HAS_OUTPUT || ds == r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT; ds = decoder.decode())
            take();
    }
    for(auto ds = decoder.flush(); ds == r_av::R_CODEC_STATE_HAS_OUTPUT || ds == r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT; ds = decoder.flush())
        take();
    return out;
}

bool near(int a, int b) { return a >= b - 2 && a <= b + 2; }

}

void test_r_recap::test_frames_decoded_show_their_times()
{
    // 4 s at 10 fps, a key frame every 2 s: every frame is decoded. Frame i is grey 16 + 5i, so a frame a step
    // early or late shows 5 off.
    grey_recording rec(40, 10, 20, [](int i) { return 16 + 5 * i; });
    auto r = rec.recap(0, 4000);
    RTF_ASSERT(r.decode_all);
    RTF_ASSERT(r.frames == 24);
    auto lumas = middle_lumas(r.mp4);
    RTF_ASSERT(lumas.size() == 24);
    // Recap frame k is the first at or after 4000 * k / 24 ms: frame ceil(40k / 24).
    for(int k = 0; k < 24; ++k)
    {
        const int i = (40 * k + 23) / 24;
        RTF_ASSERT(near(lumas[k], 16 + 5 * i));
    }
}

void test_r_recap::test_key_frames_show_their_times()
{
    // 30 s at 10 fps, a key frame every second: 30 key frames, each its second's grey (16 + 7s).
    grey_recording rec(300, 10, 10, [](int i) { return 16 + 7 * (i / 10); });
    auto r = rec.recap(0, 29900);
    RTF_ASSERT(!r.decode_all);
    RTF_ASSERT(r.frames == 30);
    auto lumas = middle_lumas(r.mp4);
    RTF_ASSERT(lumas.size() == 30);
    for(int s = 0; s < 30; ++s)
        RTF_ASSERT(near(lumas[s], 16 + 7 * s));
}
