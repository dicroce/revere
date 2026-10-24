#include "test_r_key_frame_reply.h"
#include "r_vss/r_query.h"
#include "r_storage/r_storage_file.h"
#include "r_storage/r_storage_file_reader.h"
#include "r_av/r_video_encoder.h"
#include "r_utils/r_file.h"
#include "r_utils/r_nullable.h"
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_storage;
using namespace r_av;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_key_frame_reply);

static const char* REC = "key_frame_reply_test.nts";
static const uint16_t SRC_W = 320, SRC_H = 240;

static int64_t _now_ms()
{
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// A grey picture whose brightness says which frame it is.
static vector<uint8_t> _picture(uint8_t luma)
{
    vector<uint8_t> yuv(SRC_W * SRC_H * 3 / 2, 128);
    fill(yuv.begin(), yuv.begin() + SRC_W * SRC_H, luma);
    return yuv;
}

// Records three one-second GOPs at 10 fps, as revere does: H.264 with the
// parameter sets in band on each key frame. GOP n is luma 40 + 70 * n.
static int64_t _record()
{
    r_storage_file::allocate(REC, 65536, 40);
    r_storage_file sf(REC);
    r_nullable<string> vp;
    vp.set_value(string(""));
    auto wc = sf.create_write_context("h264", vp, R_STORAGE_MEDIA_TYPE_VIDEO);

    r_video_encoder enc(AV_CODEC_ID_H264, 1000000, SRC_W, SRC_H, AVRational{10, 1}, AV_PIX_FMT_YUV420P, 0, 10,
                        AV_PROFILE_H264_HIGH, 41, "veryfast", "zerolatency");
    auto parameter_sets = enc.get_extradata();

    int64_t base = _now_ms() - 20000, ts = base;
    for(int i = 0; i < 30; ++i)
    {
        auto pic = _picture((uint8_t)(40 + 70 * (i / 10)));
        enc.attach_buffer(pic.data(), pic.size(), i);
        while(enc.encode() == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto pi = enc.get();
            vector<uint8_t> au;
            if(pi.key)
                au = parameter_sets;
            au.insert(au.end(), pi.data, pi.data + pi.size);
            ts = base + i * 100;
            sf.write_frame(wc, R_STORAGE_MEDIA_TYPE_VIDEO, au.data(), au.size(), pi.key, ts, i * 100);
        }
    }
    return ts;
}

static double _mean(const uint8_t* p, size_t n, size_t stride, size_t offset)
{
    double sum = 0;
    size_t count = 0;
    for(size_t i = offset; i < n; i += stride, ++count)
        sum += p[i];
    return count ? sum / count : 0;
}

void test_r_key_frame_reply::setup()
{
    if(r_fs::file_exists(REC))
        r_fs::remove_file(REC);
}

void test_r_key_frame_reply::teardown()
{
    if(r_fs::file_exists(REC))
        r_fs::remove_file(REC);
}

void test_r_key_frame_reply::test_newest_recorded_key_frame()
{
    auto newest = _record();

    // What GET /key_frame answers for "now", a minute after the last frame:
    // the newest key frame, the start of the third GOP.
    auto reply = r_storage_file_reader(REC).query_key(R_STORAGE_MEDIA_TYPE_VIDEO, newest + 60000);
    auto kf = decode_key_frame_reply(reply, false, 200, 200);

    RTF_ASSERT(kf.pixels != nullptr);
    RTF_ASSERT(!kf.nv12);
    RTF_ASSERT(kf.source_w == SRC_W && kf.source_h == SRC_H);
    // Fits inside 200 x 200 with the recording's 4:3 kept.
    RTF_ASSERT(kf.w == 200 && kf.h == 150);
    RTF_ASSERT(kf.pixels->size() == (size_t)kf.w * kf.h * 4);
    RTF_ASSERT(kf.ts == newest - 900);

    // BGRA grey at the third GOP's brightness (luma 180, about 191 in RGB).
    double b = _mean(kf.pixels->data(), kf.pixels->size(), 4, 0);
    double g = _mean(kf.pixels->data(), kf.pixels->size(), 4, 1);
    RTF_ASSERT(fabs(b - g) < 4.0);
    RTF_ASSERT(b > 175.0 && b < 205.0);
}

void test_r_key_frame_reply::test_nv12()
{
    auto newest = _record();
    auto reply = r_storage_file_reader(REC).query_key(R_STORAGE_MEDIA_TYPE_VIDEO, newest);
    auto kf = decode_key_frame_reply(reply, true, 640, 480);

    RTF_ASSERT(kf.pixels != nullptr);
    RTF_ASSERT(kf.nv12);
    // Never enlarged past the recording.
    RTF_ASSERT(kf.w == SRC_W && kf.h == SRC_H);
    RTF_ASSERT(kf.pixels->size() == (size_t)kf.w * kf.h * 3 / 2);
    double y = _mean(kf.pixels->data(), (size_t)kf.w * kf.h, 1, 0);
    RTF_ASSERT(y > 172.0 && y < 188.0);
}

void test_r_key_frame_reply::test_nothing_recorded()
{
    // Over 5 minutes past the newest frame, /key_frame has nothing: no picture.
    auto newest = _record();
    auto reply = r_storage_file_reader(REC).query_key(R_STORAGE_MEDIA_TYPE_VIDEO, newest + 6 * 60000);
    RTF_ASSERT(decode_key_frame_reply(reply, false, 200, 200).pixels == nullptr);
}

void test_r_key_frame_reply::test_not_a_reply()
{
    RTF_ASSERT(decode_key_frame_reply({}, false, 200, 200).pixels == nullptr);
    vector<uint8_t> junk(64, 7);
    RTF_ASSERT(decode_key_frame_reply(junk, false, 200, 200).pixels == nullptr);
    // A real reply, but no room to draw it.
    auto newest = _record();
    auto reply = r_storage_file_reader(REC).query_key(R_STORAGE_MEDIA_TYPE_VIDEO, newest);
    RTF_ASSERT(decode_key_frame_reply(reply, false, 0, 0).pixels == nullptr);
}
