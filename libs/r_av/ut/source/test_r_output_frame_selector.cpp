
#include "test_r_output_frame_selector.h"
#include "r_av/r_transcoder.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_hw_accel.h"
#include <cstring>
#include <random>
#include <vector>

using namespace std;
using namespace r_av;

REGISTER_TEST_FIXTURE(test_r_output_frame_selector);

namespace
{
const AVRational OUT_TB = {1, 15}; // encoder timebase of a 15 fps output
}

void test_r_output_frame_selector::setup()
{
}

void test_r_output_frame_selector::teardown()
{
}

void test_r_output_frame_selector::test_30_to_15_keeps_every_other_frame()
{
    // 30 fps camera, 90 kHz RTP clock. Odd frames land on half ticks, which
    // av_rescale_q rounds up, so after frame 0 the odd frames are the kept ones.
    r_output_frame_selector selector;
    int kept = 0;
    for(int i = 0; i < 300; ++i)
    {
        auto plan = selector.next(av_rescale_q((int64_t)i * 3000, AVRational{1, 90000}, OUT_TB));
        RTF_ASSERT(plan.keep == (i == 0 || i % 2 == 1));
        if(plan.keep)
        {
            RTF_ASSERT(plan.pts == kept);
            RTF_ASSERT(plan.repeats == 0);
            ++kept;
        }
    }
    RTF_ASSERT(kept == 151);
}

void test_r_output_frame_selector::test_30_to_15_millisecond_timestamps()
{
    // Millisecond pts (33, 67, 100, ...) don't divide evenly into 15 fps
    // ticks, so the kept frames aren't strictly alternate, but every output
    // tick still gets exactly one frame and half the input is dropped.
    r_output_frame_selector selector;
    int kept = 0;
    int64_t expected_pts = 0;
    for(int i = 0; i < 300; ++i)
    {
        auto plan = selector.next(av_rescale_q((int64_t)i * 1000 / 30, AVRational{1, 1000}, OUT_TB));
        if(!plan.keep)
            continue;
        ++kept;
        RTF_ASSERT(plan.repeats == 0);
        RTF_ASSERT(plan.pts == expected_pts);
        ++expected_pts;
    }
    RTF_ASSERT(kept >= 149 && kept <= 151);
}

void test_r_output_frame_selector::test_jitter_fills_every_tick_once()
{
    // Camera timestamps wander up to +/- 8 ms around 30 fps. Output ticks must
    // stay contiguous: each one is either a kept frame or a repeat, never both,
    // never skipped, and roughly half the input is still dropped.
    mt19937 rng(42);
    uniform_int_distribution<int> jitter(-8, 8);

    r_output_frame_selector selector;
    int kept = 0;
    int64_t covered = 0;
    int64_t next_tick = -1;
    for(int i = 0; i < 600; ++i)
    {
        int64_t pts_ms = 1000 + (int64_t)i * 1000 / 30 + jitter(rng);
        auto plan = selector.next(av_rescale_q(pts_ms, AVRational{1, 1000}, OUT_TB));
        if(!plan.keep)
            continue;
        if(next_tick == -1)
            next_tick = plan.pts;
        RTF_ASSERT(plan.repeats >= 0 && plan.repeats <= 1);
        RTF_ASSERT(plan.pts - plan.repeats == next_tick);
        next_tick = plan.pts + 1;
        covered += plan.repeats + 1;
        ++kept;
    }
    RTF_ASSERT(covered >= 299 && covered <= 301);
    RTF_ASSERT(kept >= 280 && kept <= 300);
}

void test_r_output_frame_selector::test_gap_repeats_previous_frame()
{
    r_output_frame_selector selector;
    for(int64_t t = 0; t < 10; ++t)
        RTF_ASSERT(selector.next(t).keep);

    // Camera stalls for 5 ticks: the frame after the gap is kept, and the four
    // missing ticks before it are filled with the previous frame.
    auto plan = selector.next(14);
    RTF_ASSERT(plan.keep);
    RTF_ASSERT(plan.pts == 14);
    RTF_ASSERT(plan.repeats == 4);

    // A late frame for a tick already filled is dropped
    RTF_ASSERT(!selector.next(13).keep);
    RTF_ASSERT(!selector.next(14).keep);
    RTF_ASSERT(selector.next(15).keep);
}

void test_r_output_frame_selector::test_long_gap_jumps_without_repeats()
{
    // At 15 fps with a one second limit, the input skipping 2.5 s ahead (a
    // live stream catching up to its newest keyframe) is a jump: the frame
    // after it is kept at its own pts, and nothing is repeated.
    r_output_frame_selector selector(15);
    RTF_ASSERT(selector.next(0).keep);

    auto stall = selector.next(16); // 15 missing ticks: a stall, filled
    RTF_ASSERT(stall.keep);
    RTF_ASSERT(stall.repeats == 15);

    auto jump = selector.next(16 + 38); // 37 missing ticks: a jump
    RTF_ASSERT(jump.keep);
    RTF_ASSERT(jump.pts == 54);
    RTF_ASSERT(jump.repeats == 0);

    // and the output clock carries on from the new position
    RTF_ASSERT(!selector.next(54).keep);
    auto after = selector.next(55);
    RTF_ASSERT(after.keep && after.repeats == 0 && after.pts == 55);
}

void test_r_output_frame_selector::test_reset_starts_over()
{
    r_output_frame_selector selector;
    RTF_ASSERT(selector.next(100).keep);
    RTF_ASSERT(!selector.next(50).keep);

    selector.reset();

    auto plan = selector.next(50);
    RTF_ASSERT(plan.keep);
    RTF_ASSERT(plan.pts == 50);
    RTF_ASSERT(plan.repeats == 0);
}

void test_r_output_frame_selector::test_hw_decode_get_only_kept_frames()
{
    // The transcoder decodes every frame but calls get() (the hw download and
    // scale) only for kept ones. With the machine's hardware decoder, if it has
    // one, skipped frames must not break later gets: each kept frame comes back
    // as the right picture at the requested size.
    const uint16_t W = 1280, H = 720;
    const AVRational fps = {30, 1};

    r_video_encoder encoder(AV_CODEC_ID_H264, 2000000, W, H, fps, AV_PIX_FMT_YUV420P,
                            0, 30, AV_PROFILE_H264_MAIN, 41, "veryfast", "zerolatency",
                            r_hw_accel::none);

    r_video_decoder decoder(AV_CODEC_ID_H264, r_find_best_hw_accel(AV_CODEC_ID_H264));
    decoder.set_extradata(encoder.get_extradata());

    const size_t y_size = (size_t)W * H;
    vector<uint8_t> raw(y_size * 3 / 2);
    memset(raw.data() + y_size, 128, y_size / 2);

    int decoded = 0, gotten = 0;
    for(int i = 0; i < 60; ++i)
    {
        // Luma steps with the frame number, so a stale or wrong frame shows
        uint8_t luma = (uint8_t)(40 + (i % 8) * 20);
        memset(raw.data(), luma, y_size);

        encoder.attach_buffer(raw.data(), raw.size(), i);
        while(encoder.encode() == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto pi = encoder.get();
            decoder.attach_buffer(pi.data, pi.size);
            auto state = decoder.decode();
            if(state != R_CODEC_STATE_HAS_OUTPUT && state != R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                continue;
            int n = decoded++;
            if(n % 2 != 0)
                continue;

            auto out = decoder.get(AV_PIX_FMT_YUV420P, 320, 240, 1);
            ++gotten;
            RTF_ASSERT(out->size() == (size_t)320 * 240 * 3 / 2);

            // zerolatency: one packet in, the same frame out
            uint8_t expected = (uint8_t)(40 + (n % 8) * 20);
            int y = (*out)[120 * 320 + 160];
            RTF_ASSERT(abs(y - (int)expected) <= 6);
        }
    }
    RTF_ASSERT(decoded >= 55);
    RTF_ASSERT(gotten >= 27);
}
