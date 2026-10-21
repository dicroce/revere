
#include "test_r_codec.h"
#include "bad_guy.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include <algorithm>
#include <vector>
#include "r_av/r_demuxer.h"
#include "r_av/r_muxer.h"
#include "r_utils/r_file.h"
#ifndef IS_WINDOWS
#include <sys/mman.h>
#include <unistd.h>
#endif

// Added to the global namespace by test_r_mux.cpp, so extern'd here:
extern unsigned char true_north_mp4[];
extern unsigned int true_north_mp4_len;
extern unsigned char bad_guy_mp4[];
extern unsigned int bad_guy_mp4_len;

using namespace std;
using namespace std::chrono;
using namespace r_utils;
using namespace r_av;
using namespace r_av;

REGISTER_TEST_FIXTURE(test_r_codec);

void test_r_codec::setup()
{
    r_fs::write_file(true_north_mp4, true_north_mp4_len, "true_north.mp4");
    r_fs::write_file(bad_guy_mp4, bad_guy_mp4_len, "bad_guy.mp4");
}

void test_r_codec::teardown()
{
    r_fs::remove_file("true_north.mp4");
    r_fs::remove_file("bad_guy.mp4");
    if(r_fs::file_exists("truer_north.mp4"))
        r_fs::remove_file("truer_north.mp4");
}

void test_r_codec::test_basic_video_decode()
{
    r_demuxer demuxer("true_north.mp4", true);
    auto video_stream_index = demuxer.get_video_stream_index();
    auto vsi = demuxer.get_stream_info(video_stream_index);

    r_video_decoder decoder(vsi.codec_id);

    while(demuxer.read_frame())
    {
        auto fi = demuxer.get_frame_info();

        if(fi.index == video_stream_index)
        {
            decoder.attach_buffer(fi.data, fi.size);

            r_codec_state decode_state = R_CODEC_STATE_INITIALIZED;

            while(decode_state != R_CODEC_STATE_HUNGRY)
            {
                decode_state = decoder.decode();

                if(decode_state == R_CODEC_STATE_HAS_OUTPUT)
                {
                    auto frame = decoder.get(AV_PIX_FMT_ARGB, 640, 480, 1);
                    RTF_ASSERT(frame->size() > 0);
                }
            }
        }
    }
}

void test_r_codec::test_basic_video_transcode()
{
    {
        r_demuxer demuxer("true_north.mp4", true);
        auto video_stream_index = demuxer.get_video_stream_index();
        auto vsi = demuxer.get_stream_info(video_stream_index);

        RTF_ASSERT(vsi.resolution.first == 640);
        RTF_ASSERT(vsi.resolution.second == 360);

        r_video_decoder decoder(vsi.codec_id);
        decoder.set_extradata(demuxer.get_extradata(video_stream_index));
        r_video_encoder encoder(AV_CODEC_ID_H264, 1000000, (uint16_t)320, (uint16_t)240, vsi.frame_rate, AV_PIX_FMT_YUV420P, 0, (uint16_t)vsi.frame_rate.num, vsi.profile, vsi.level);

        r_muxer muxer("truer_north.mp4");
        muxer.add_video_stream(vsi.frame_rate, AV_CODEC_ID_H264, (uint16_t)320, (uint16_t)240, vsi.profile, vsi.level);
        muxer.set_video_extradata(encoder.get_extradata());
        muxer.open();

        r_frame_info fi;
        int64_t frame_num = 0;

        while(demuxer.read_frame())
        {
            fi = demuxer.get_frame_info();

            if(fi.index == video_stream_index)
            {
                decoder.attach_buffer(fi.data, fi.size);

                auto decode_state = decoder.decode();

                if(decode_state == R_CODEC_STATE_AGAIN)
                    continue;

                if(decode_state == R_CODEC_STATE_HAS_OUTPUT || decode_state == R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                {
                    auto frame = decoder.get(AV_PIX_FMT_YUV420P, 320, 240, 1);

                    int encode_attempts = 10;

    ENCODE_AGAIN_TOP:
                    --encode_attempts;
                    encoder.attach_buffer(frame->data(), frame->size(), frame_num);
                    frame_num++;

                    auto encode_state = encoder.encode();

                    if(encode_state == R_CODEC_STATE_AGAIN && encode_attempts > 0)
                        goto ENCODE_AGAIN_TOP;

                    if(encode_state == R_CODEC_STATE_HAS_OUTPUT)
                    {
                        auto pi = encoder.get();
                        muxer.write_video_frame(pi.data, pi.size, pi.pts, pi.dts, pi.time_base, pi.key);
                    }

                    if(decode_state == R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                        continue;
                }
            }
        }

        auto decode_state = decoder.flush();
        if(decode_state == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto frame = decoder.get(AV_PIX_FMT_YUV420P, 320, 240, 1);

            int encode_attempts = 10;

    ENCODE_AGAIN_BOTTOM:
            --encode_attempts;
            encoder.attach_buffer(frame->data(), frame->size(), frame_num);
            frame_num++;

            auto encode_state = encoder.encode();

            if(encode_state == R_CODEC_STATE_AGAIN && encode_attempts > 0)
                goto ENCODE_AGAIN_BOTTOM;

            if(encode_state == R_CODEC_STATE_HAS_OUTPUT)
            {
                auto pi = encoder.get();
                muxer.write_video_frame(pi.data, pi.size, pi.pts, pi.dts, pi.time_base, pi.key);
            }
        }

        muxer.finalize();
    }

    r_demuxer demuxer("truer_north.mp4", true);
    auto video_stream_index = demuxer.get_video_stream_index();
    auto vsi = demuxer.get_stream_info(video_stream_index);
    RTF_ASSERT(vsi.resolution.first == 320);
    RTF_ASSERT(vsi.resolution.second == 240);

}


// A JPEG snapshot must be encoded at a usable quality. Encode a synthetic
// frame and read the luma quantization table (DQT) out of the JPEG: the DC
// entry of the standard IJG table is 16 at quality 50 and scales inversely
// with quality. The old rate-controlled path pinned the quantizer and
// produced DC entries around 35 (quality ~20); qscale 3 gives ~6.
void test_r_codec::test_mjpeg_snapshot_quality()
{
    const uint16_t w = 320, h = 240;
    std::vector<uint8_t> yuv(w * h * 3 / 2);
    for(int y = 0; y < h; ++y)
        for(int x = 0; x < w; ++x)
            yuv[y * w + x] = (uint8_t)((x * 255) / w);          // horizontal luma ramp
    std::fill(yuv.begin() + w * h, yuv.end(), 128);             // neutral chroma

    r_video_encoder encoder(AV_CODEC_ID_MJPEG, 0, w, h, {1,1}, AV_PIX_FMT_YUVJ420P, 0, 1, 0, 0);
    encoder.attach_buffer(yuv.data(), yuv.size(), 0);
    RTF_ASSERT(encoder.encode() == R_CODEC_STATE_HAS_OUTPUT);
    auto pi = encoder.get();
    RTF_ASSERT(pi.size > 4);
    RTF_ASSERT(pi.data[0] == 0xFF && pi.data[1] == 0xD8); // SOI

    // Walk markers to the first DQT (0xFFDB); table 0 is luma.
    const uint8_t* p = pi.data + 2;
    const uint8_t* end = pi.data + pi.size;
    int dc_quant = -1;
    while(p + 4 <= end && p[0] == 0xFF)
    {
        uint8_t marker = p[1];
        uint16_t len = (uint16_t)((p[2] << 8) | p[3]);
        if(marker == 0xDB)
        {
            uint8_t pq_tq = p[4];
            RTF_ASSERT((pq_tq & 0x0F) == 0); // first table is luma
            dc_quant = (pq_tq >> 4) ? ((p[5] << 8) | p[6]) : p[5];
            break;
        }
        if(marker == 0xDA) break; // start of scan, no DQT found
        p += 2 + len;
    }
    RTF_ASSERT(dc_quant > 0);
    RTF_ASSERT(dc_quant <= 8);   // ~quality 85+; the regression produced ~35
}

namespace
{

const uint16_t PAD_W = 320, PAD_H = 240;

// A picture to check the decode against: a gradient with a bright block.
vector<uint8_t> pad_test_picture()
{
    vector<uint8_t> yuv(PAD_W * PAD_H * 3 / 2, 128);
    for(int y = 0; y < PAD_H; ++y)
        for(int x = 0; x < PAD_W; ++x)
            yuv[y * PAD_W + x] = (uint8_t)(16 + ((x + y) % 200));
    for(int y = 80; y < 160; ++y)
        for(int x = 120; x < 200; ++x)
            yuv[y * PAD_W + x] = 235;
    return yuv;
}

// Mean absolute luma difference between a decode and the source picture.
double luma_error(const vector<uint8_t>& a, const vector<uint8_t>& b)
{
    double sum = 0;
    for(size_t i = 0; i < (size_t)PAD_W * PAD_H; ++i)
        sum += abs((int)a[i] - (int)b[i]);
    return sum / (PAD_W * PAD_H);
}

#ifndef IS_WINDOWS
// `data` copied so its last byte is the last readable byte: the next page is
// PROT_NONE, so reading even one byte past the end faults.
struct guarded_copy
{
    uint8_t* base {nullptr};
    size_t mapped {0};
    const uint8_t* data {nullptr};

    explicit guarded_copy(const vector<uint8_t>& src)
    {
        size_t page = (size_t)sysconf(_SC_PAGESIZE);
        size_t pages = (src.size() + page - 1) / page + 1;
        mapped = pages * page;
        void* p = mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        RTF_ASSERT(p != MAP_FAILED);
        base = (uint8_t*)p;
        RTF_ASSERT(mprotect(base + mapped - page, page, PROT_NONE) == 0);
        uint8_t* start = base + mapped - page - src.size();
        memcpy(start, src.data(), src.size());
        data = start;
    }
    ~guarded_copy()
    {
        if(base)
            munmap(base, mapped);
    }
};
#endif

}

// FFmpeg reads up to AV_INPUT_BUFFER_PADDING_SIZE past the end of its input.
// A key frame whose buffer ends exactly at a page boundary (a stored frame in
// a vector) used to fault there, or read a neighbour's heap. Decoded in parse
// mode, in direct mode with the parameter sets as extradata (set_extradata()
// copies them, so this checks decoding with extradata, not its padding), and
// through a decoder moved between attach_buffer() and decode().
void test_r_codec::test_decode_input_ending_at_a_page()
{
#ifndef IS_WINDOWS
    auto src = pad_test_picture();

    r_video_encoder enc(AV_CODEC_ID_H264, 1000000, PAD_W, PAD_H, AVRational{30, 1}, AV_PIX_FMT_YUV420P, 0, 30,
                        AV_PROFILE_H264_HIGH, 41, "veryfast", "zerolatency");
    auto parameter_sets = enc.get_extradata();
    RTF_ASSERT(parameter_sets.empty() || parameter_sets[0] == 0);  // Annex B
    enc.attach_buffer(src.data(), src.size(), 0);
    vector<uint8_t> idr;
    while(enc.encode() == R_CODEC_STATE_HAS_OUTPUT)
    {
        auto pi = enc.get();
        idr.assign(pi.data, pi.data + pi.size);
    }
    while(idr.empty() && enc.flush() == R_CODEC_STATE_HAS_OUTPUT)
    {
        auto pi = enc.get();
        idr.assign(pi.data, pi.data + pi.size);
    }
    RTF_ASSERT(!idr.empty());

    auto decode_all = [](r_video_decoder& dec) -> shared_ptr<vector<uint8_t>> {
        for(int i = 0; i < 10; ++i)
        {
            auto st = dec.decode();
            if(st == R_CODEC_STATE_HAS_OUTPUT || st == R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                return dec.get(AV_PIX_FMT_YUV420P, PAD_W, PAD_H, 1);
        }
        for(int i = 0; i < 10; ++i)
            if(dec.flush() == R_CODEC_STATE_HAS_OUTPUT)
                return dec.get(AV_PIX_FMT_YUV420P, PAD_W, PAD_H, 1);
        return nullptr;
    };

    // Parse mode, parameter sets in band: what r_query's single-frame decode does.
    {
        vector<uint8_t> frame = parameter_sets;
        frame.insert(frame.end(), idr.begin(), idr.end());
        guarded_copy g(frame);
        r_video_decoder dec(AV_CODEC_ID_H264, r_hw_accel::none, true);
        dec.attach_buffer(g.data, frame.size());
        auto out = decode_all(dec);
        RTF_ASSERT(out && luma_error(*out, src) < 6.0);
    }

    // Direct mode, parameter sets as extradata.
    if(!parameter_sets.empty())
    {
        guarded_copy g(idr);
        r_video_decoder dec(AV_CODEC_ID_H264);
        dec.set_extradata(parameter_sets);
        dec.attach_buffer(g.data, idr.size());
        auto out = decode_all(dec);
        RTF_ASSERT(out && luma_error(*out, src) < 6.0);
    }

    // Moved between attach_buffer() and decode(): the padded copy moves with it.
    {
        vector<uint8_t> frame = parameter_sets;
        frame.insert(frame.end(), idr.begin(), idr.end());
        r_video_decoder first(AV_CODEC_ID_H264, r_hw_accel::none, true);
        {
            guarded_copy g(frame);
            first.attach_buffer(g.data, frame.size());
        }   // the caller's buffer is gone; the decoder decodes its own copy
        r_video_decoder moved(std::move(first));
        auto out = decode_all(moved);
        RTF_ASSERT(out && luma_error(*out, src) < 6.0);
    }
#endif
}
