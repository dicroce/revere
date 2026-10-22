#include "test_r_h264_frame_num.h"
#include "r_av/r_h264_frame_num.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_hw_accel.h"
#include <cstdlib>
#include <vector>

using namespace std;
using namespace r_av;

REGISTER_TEST_FIXTURE(test_r_h264_frame_num);

namespace
{

class bit_writer
{
public:
    void u(uint32_t v, int n)
    {
        for(int i = n - 1; i >= 0; --i)
            _bits.push_back((v >> i) & 1);
    }
    void ue(uint32_t v)
    {
        uint32_t x = v + 1;
        int len = 0;
        while((x >> len) > 1)
            ++len;
        u(0, len);
        u(x, len + 1);
    }
    void se(int32_t v)
    {
        ue(v > 0 ? (uint32_t)(2 * v - 1) : (uint32_t)(-2 * v));
    }
    // rbsp_trailing_bits, then the bytes
    vector<uint8_t> rbsp()
    {
        u(1, 1);
        while(_bits.size() % 8)
            _bits.push_back(0);
        vector<uint8_t> out(_bits.size() / 8, 0);
        for(size_t i = 0; i < _bits.size(); ++i)
            out[i / 8] |= _bits[i] << (7 - (i % 8));
        return out;
    }
private:
    vector<uint8_t> _bits;
};

// Start code + header + payload, with emulation prevention bytes inserted.
vector<uint8_t> nal(uint8_t header, const vector<uint8_t>& rbsp)
{
    vector<uint8_t> out = {0, 0, 0, 1, header};
    int zeros = 0;
    for(auto b : rbsp)
    {
        if(zeros >= 2 && b <= 3)
        {
            out.push_back(3);
            zeros = 0;
        }
        out.push_back(b);
        zeros = (b == 0) ? zeros + 1 : 0;
    }
    return out;
}

struct sps_opts
{
    uint32_t profile {66};
    uint32_t constraint {0};
    uint32_t level {30};
    uint32_t id {0};
    int log2_max_frame_num {4};
    bool gaps_allowed {false};
    bool scaling_matrix {false};
};

vector<uint8_t> sps(const sps_opts& o)
{
    bit_writer w;
    w.u(o.profile, 8);
    w.u(o.constraint, 8);
    w.u(o.level, 8);
    w.ue(o.id);
    if(o.profile == 100)
    {
        w.ue(1);  // chroma_format_idc
        w.ue(0);
        w.ue(0);
        w.u(0, 1);
        w.u(o.scaling_matrix ? 1 : 0, 1);
        if(o.scaling_matrix)
        {
            for(int l = 0; l < 8; ++l)
            {
                bool present = (l == 0 || l == 6);
                w.u(present ? 1 : 0, 1);
                if(present)
                    for(int j = 0; j < (l < 6 ? 16 : 64); ++j)
                        w.se(j % 2 ? 3 : -2);  // never zero, so the whole list is coded
            }
        }
    }
    w.ue(o.log2_max_frame_num - 4);
    w.ue(0);  // pic_order_cnt_type
    w.ue(2);  // log2_max_pic_order_cnt_lsb_minus4
    w.ue(1);  // max_num_ref_frames
    w.u(o.gaps_allowed ? 1 : 0, 1);
    w.ue(39); // pic_width_in_mbs_minus1
    w.ue(29);
    w.u(1, 1); // frame_mbs_only_flag
    w.u(1, 1);
    w.u(0, 1);
    w.u(0, 1);
    return nal(0x67, w.rbsp());
}

vector<uint8_t> pps(uint32_t id = 0, uint32_t sps_id = 0)
{
    bit_writer w;
    w.ue(id);
    w.ue(sps_id);
    w.u(0, 1);
    w.u(0, 1);
    return nal(0x68, w.rbsp());
}

vector<uint8_t> slice(bool idr, bool ref, uint32_t frame_num, int log2_max_frame_num, uint32_t first_mb = 0)
{
    bit_writer w;
    w.ue(first_mb);
    w.ue(idr ? 7 : 5);
    w.ue(0);  // pps id
    w.u(frame_num, log2_max_frame_num);
    if(idr)
        w.ue(0);  // idr_pic_id
    w.u(0xabcd, 16);  // stand-in for the rest of the slice
    return nal((uint8_t)((ref ? 0x60 : 0) | (idr ? 5 : 1)), w.rbsp());
}

vector<uint8_t> cat(initializer_list<vector<uint8_t>> parts)
{
    vector<uint8_t> out;
    for(auto& p : parts)
        out.insert(out.end(), p.begin(), p.end());
    return out;
}

r_h264_frame_num_tracker::result feed(r_h264_frame_num_tracker& t, const vector<uint8_t>& au)
{
    return t.feed(au.data(), au.size());
}

// Feeds an IDR access unit with parameter sets, then reference P pictures
// with the given frame_nums; returns how many of them read as a gap.
int gaps_in(r_h264_frame_num_tracker& t, const sps_opts& o, const vector<uint32_t>& frame_nums)
{
    RTF_ASSERT(feed(t, cat({sps(o), pps(), slice(true, true, 0, o.log2_max_frame_num)})) == r_h264_frame_num_tracker::IDR);
    int gaps = 0;
    for(auto fn : frame_nums)
    {
        auto r = feed(t, slice(false, true, fn, o.log2_max_frame_num));
        RTF_ASSERT(r == r_h264_frame_num_tracker::IN_ORDER || r == r_h264_frame_num_tracker::GAP);
        if(r == r_h264_frame_num_tracker::GAP)
            ++gaps;
    }
    return gaps;
}

vector<uint32_t> run(uint32_t from, uint32_t count, uint32_t modulo)
{
    vector<uint32_t> v;
    for(uint32_t i = 0; i < count; ++i)
        v.push_back((from + i) % modulo);
    return v;
}

}

void test_r_h264_frame_num::test_in_order_with_wrap()
{
    r_h264_frame_num_tracker t;
    sps_opts o;  // frame_num modulo 16
    RTF_ASSERT(gaps_in(t, o, run(1, 50, 16)) == 0);
}

void test_r_h264_frame_num::test_lost_picture_is_a_gap()
{
    r_h264_frame_num_tracker t;
    sps_opts o;
    RTF_ASSERT(gaps_in(t, o, {1, 2, 3, 5, 6, 7}) == 1);  // 4 never arrived
    // ...and only the picture after the loss reads as one.
    RTF_ASSERT(feed(t, slice(false, true, 8, 4)) == r_h264_frame_num_tracker::IN_ORDER);
    // Lost across the wrap (15 -> 1).
    r_h264_frame_num_tracker t2;
    RTF_ASSERT(gaps_in(t2, o, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 1}) == 1);
}

void test_r_h264_frame_num::test_lost_idr_is_a_gap()
{
    // GOP of 8: frame_num 0..7, then the next IDR is lost along with the
    // picture after it; the next picture to arrive is frame_num 2 of the new GOP.
    r_h264_frame_num_tracker t;
    sps_opts o;
    RTF_ASSERT(gaps_in(t, o, {1, 2, 3, 4, 5, 6, 7, 2}) == 1);
    // Until an IDR, the stream stays judged against what arrived.
    RTF_ASSERT(feed(t, slice(false, true, 3, 4)) == r_h264_frame_num_tracker::IN_ORDER);
    RTF_ASSERT(feed(t, slice(true, true, 0, 4)) == r_h264_frame_num_tracker::IDR);
    RTF_ASSERT(feed(t, slice(false, true, 1, 4)) == r_h264_frame_num_tracker::IN_ORDER);
}

void test_r_h264_frame_num::test_non_reference_pictures()
{
    r_h264_frame_num_tracker t;
    sps_opts o;
    RTF_ASSERT(gaps_in(t, o, {1, 2}) == 0);
    // Non-reference pictures carry last reference + 1 and don't advance it.
    RTF_ASSERT(feed(t, slice(false, false, 3, 4)) == r_h264_frame_num_tracker::IN_ORDER);
    RTF_ASSERT(feed(t, slice(false, false, 3, 4)) == r_h264_frame_num_tracker::IN_ORDER);
    RTF_ASSERT(feed(t, slice(false, true, 3, 4)) == r_h264_frame_num_tracker::IN_ORDER);
    RTF_ASSERT(feed(t, slice(false, true, 4, 4)) == r_h264_frame_num_tracker::IN_ORDER);
    // A lost non-reference picture leaves no gap (nothing refers to it)...
    RTF_ASSERT(feed(t, slice(false, true, 5, 4)) == r_h264_frame_num_tracker::IN_ORDER);
    // ...a lost reference picture does, even if a non-reference one comes next.
    RTF_ASSERT(feed(t, slice(false, false, 7, 4)) == r_h264_frame_num_tracker::GAP);
}

void test_r_h264_frame_num::test_multi_slice_counts_once()
{
    r_h264_frame_num_tracker t;
    sps_opts o;
    RTF_ASSERT(gaps_in(t, o, {1}) == 0);
    // One access unit, three slices of picture 2.
    auto au = cat({slice(false, true, 2, 4, 0), slice(false, true, 2, 4, 400), slice(false, true, 2, 4, 800)});
    RTF_ASSERT(feed(t, au) == r_h264_frame_num_tracker::IN_ORDER);
    // A buffer holding only a later slice is not a new picture.
    RTF_ASSERT(feed(t, slice(false, true, 2, 4, 400)) == r_h264_frame_num_tracker::NO_PICTURE);
    // Parameter sets alone aren't either.
    RTF_ASSERT(feed(t, cat({sps(o), pps()})) == r_h264_frame_num_tracker::NO_PICTURE);
    RTF_ASSERT(feed(t, slice(false, true, 3, 4)) == r_h264_frame_num_tracker::IN_ORDER);
}

void test_r_h264_frame_num::test_sps_change_waits_for_idr()
{
    r_h264_frame_num_tracker t;
    sps_opts o;
    RTF_ASSERT(gaps_in(t, o, {1, 2}) == 0);
    // Pictures before any IDR can't be judged.
    r_h264_frame_num_tracker fresh;
    RTF_ASSERT(feed(fresh, cat({sps(o), pps(), slice(false, true, 5, 4)})) == r_h264_frame_num_tracker::UNKNOWN);
    // A new SPS with a different frame_num size: nothing to judge until its IDR.
    sps_opts wider = o;
    wider.log2_max_frame_num = 8;
    RTF_ASSERT(feed(t, cat({sps(wider), pps(), slice(false, true, 77, 8)})) == r_h264_frame_num_tracker::UNKNOWN);
    RTF_ASSERT(feed(t, slice(true, true, 0, 8)) == r_h264_frame_num_tracker::IDR);
    RTF_ASSERT(gaps_in(t, wider, run(1, 300, 256)) == 0);
}

void test_r_h264_frame_num::test_parameter_sets_kept()
{
    r_h264_frame_num_tracker t;
    RTF_ASSERT(t.parameter_sets().empty());
    sps_opts o;
    sps_opts other = o;
    other.log2_max_frame_num = 7;
    feed(t, cat({sps(o), pps(), slice(true, true, 0, 4)}));
    // A newer SPS of the same id replaces it; parameter sets come back SPS first.
    feed(t, cat({pps(), sps(other)}));
    RTF_ASSERT(t.parameter_sets() == cat({sps(other), pps()}));
}

void test_r_h264_frame_num::test_gaps_allowed()
{
    r_h264_frame_num_tracker t;
    sps_opts o;
    o.gaps_allowed = true;
    RTF_ASSERT(gaps_in(t, o, {1, 2, 5, 9}) == 0);
}

void test_r_h264_frame_num::test_high_profile_scaling_matrix()
{
    // log2_max_frame_num sits after the scaling lists: misreading them misreads it.
    r_h264_frame_num_tracker t;
    sps_opts o;
    o.profile = 100;
    o.scaling_matrix = true;
    o.log2_max_frame_num = 5;
    RTF_ASSERT(gaps_in(t, o, run(1, 70, 32)) == 0);
    RTF_ASSERT(gaps_in(t, o, {1, 2, 4}) == 1);
}

void test_r_h264_frame_num::test_emulation_prevention()
{
    // profile, constraint and level all zero: the SPS bytes start 00 00 00,
    // which go out as 00 00 03 00. Reading through the 03 misreads the rest.
    r_h264_frame_num_tracker t;
    sps_opts o;
    o.profile = 0;
    o.level = 0;
    o.log2_max_frame_num = 6;
    auto s = sps(o);
    RTF_ASSERT(s[5] == 0 && s[6] == 0 && s[7] == 3);
    RTF_ASSERT(gaps_in(t, o, run(1, 130, 64)) == 0);
}

namespace
{

// A test picture: black, with a white block whose position counts the frames.
const uint16_t W = 640, H = 480;
const int BLOCK = 32, STEP = 2;  // 240 frames never wrap: 16 + 2 * 240 + 32 < 640

vector<uint8_t> picture(int i)
{
    vector<uint8_t> yuv(W * H * 3 / 2, 128);
    fill(yuv.begin(), yuv.begin() + W * H, 16);
    int x0 = 16 + i * STEP, y0 = 200;
    for(int y = y0; y < y0 + BLOCK; ++y)
        for(int x = x0; x < x0 + BLOCK; ++x)
            yuv[y * W + x] = 235;
    return yuv;
}

// The frame a decoded picture shows, from its block's left edge (-1: none found).
int frame_shown(const vector<uint8_t>& yuv)
{
    long sum = 0, n = 0;
    for(int y = 200; y < 200 + BLOCK; ++y)
        for(int x = 0; x < W; ++x)
            if(yuv[y * W + x] > 128)
            {
                sum += x;
                ++n;
            }
    if(n < BLOCK * BLOCK / 2)
        return -1;
    double left = (double)sum / n - (BLOCK - 1) / 2.0;
    return (int)((left - 16) / STEP + 0.5);
}

// avcC extradata (VideoToolbox's) as Annex B parameter sets.
vector<uint8_t> annex_b_parameter_sets(const vector<uint8_t>& ed)
{
    if(ed.empty() || ed[0] != 1)
        return ed;  // already Annex B
    vector<uint8_t> out;
    size_t i = 5;
    for(int list = 0; list < 2 && i < ed.size(); ++list)
    {
        int n = (list == 0) ? (ed[i++] & 0x1f) : ed[i++];
        for(int k = 0; k < n && i + 2 <= ed.size(); ++k)
        {
            size_t len = (ed[i] << 8) | ed[i + 1];
            i += 2;
            out.insert(out.end(), {0, 0, 0, 1});
            out.insert(out.end(), ed.begin() + i, ed.begin() + i + len);
            i += len;
        }
    }
    return out;
}

// Annex B access units, the encoder's parameter sets in front of each keyframe.
// x264 without B-frames uses pic_order_cnt_type 2 (order from frame_num);
// cameras and VideoToolbox's encoder send type 0, explicit picture order counts.
vector<vector<uint8_t>> encode_stream(int frames, int gop, r_hw_accel accel = r_hw_accel::none)
{
    r_video_encoder enc(AV_CODEC_ID_H264, 2000000, W, H, AVRational{30, 1}, AV_PIX_FMT_YUV420P, 0, (uint16_t)gop,
                        accel == r_hw_accel::none ? AV_PROFILE_H264_HIGH : AV_PROFILE_H264_MAIN, 41,
                        accel == r_hw_accel::none ? "veryfast" : "", accel == r_hw_accel::none ? "zerolatency" : "", accel);
    auto ps = annex_b_parameter_sets(enc.get_extradata());
    vector<vector<uint8_t>> aus;
    auto take = [&](){
        auto pi = enc.get();
        vector<uint8_t> au;
        if(pi.key)
            au = ps;
        au.insert(au.end(), pi.data, pi.data + pi.size);
        aus.push_back(au);
    };
    for(int i = 0; i < frames; ++i)
    {
        auto p = picture(i);
        enc.attach_buffer(p.data(), p.size(), i);
        while(enc.encode() == R_CODEC_STATE_HAS_OUTPUT)
            take();
    }
    while(enc.flush() == R_CODEC_STATE_HAS_OUTPUT)
        take();
    RTF_ASSERT((int)aus.size() == frames);
    return aus;
}

}

void test_r_h264_frame_num::test_real_encoder_stream()
{
    auto aus = encode_stream(100, 30);
    r_h264_frame_num_tracker t;
    int idrs = 0;
    for(size_t i = 0; i < aus.size(); ++i)
    {
        auto r = feed(t, aus[i]);
        if(r == r_h264_frame_num_tracker::IDR)
            ++idrs;
        else
            RTF_ASSERT(r == r_h264_frame_num_tracker::IN_ORDER);
    }
    RTF_ASSERT(idrs == 4);

    // The same stream with frames 40 and 41 lost: one gap, at 42.
    r_h264_frame_num_tracker lossy;
    for(size_t i = 0; i < aus.size(); ++i)
    {
        if(i == 40 || i == 41)
            continue;
        auto r = feed(lossy, aus[i]);
        RTF_ASSERT((r == r_h264_frame_num_tracker::GAP) == (i == 42));
    }
}

// After lost frames on VideoToolbox: output is held back, then decoded in
// software when the next IDR is far off, then hardware again from the IDR;
// and what does come out never goes back in time. (The stale pictures
// themselves come from cameras that pick each P-frame's reference explicitly,
// ref_pic_list_modification plus MMCO 1, which a lost frame leaves pointing at
// the wrong picture; the encoders available here don't code references that
// way, so this checks the handling, not the trigger.) Only where the platform
// decodes H.264 with VideoToolbox.
void test_r_h264_frame_num::test_videotoolbox_gap_handling()
{
    if(r_find_best_hw_accel(AV_CODEC_ID_H264) != r_hw_accel::videotoolbox)
        return;

    // GOP of 120 (4 s at 30 fps), so the freeze runs into the software fallback.
    auto aus = encode_stream(240, 120, r_hw_accel::videotoolbox);
    vector<int> lost = {20, 21, 22, 150};

    r_video_decoder dec(AV_CODEC_ID_H264, r_hw_accel::videotoolbox);
    int newest = -1, shown = 0, worst_step_back = 0;
    for(int i = 0; i < (int)aus.size(); ++i)
    {
        if(find(lost.begin(), lost.end(), i) != lost.end())
            continue;
        dec.attach_buffer(aus[i].data(), aus[i].size());
        auto st = dec.decode();
        if(st != R_CODEC_STATE_HAS_OUTPUT && st != R_CODEC_STATE_AGAIN_HAS_OUTPUT)
            continue;
        auto f = frame_shown(*dec.get(AV_PIX_FMT_YUV420P, W, H, 1));
        ++shown;
        if(f < 0)
            continue;
        worst_step_back = max(worst_step_back, newest - f);
        newest = max(newest, f);
    }
    auto stats = dec.gap_stats();
    RTF_ASSERT(stats.gaps == 2);
    RTF_ASSERT(stats.hidden_pictures > 0);
    RTF_ASSERT(stats.software_pictures > 0);  // the first gap is ~100 frames from the next IDR
    RTF_ASSERT(shown > 0);
    // Concealment may lag the block by the lost frames; it must never jump back further.
    RTF_ASSERT(worst_step_back <= 3);
}
