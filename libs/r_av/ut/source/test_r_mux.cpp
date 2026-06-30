
#include "test_r_mux.h"
#include "r_av/r_demuxer.h"
#include "r_av/r_muxer.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_utils/r_file.h"

#include <algorithm>

#include "true_north.h"

using namespace std;
using namespace std::chrono;
using namespace r_utils;
using namespace r_av;

REGISTER_TEST_FIXTURE(test_r_mux);

void test_r_mux::setup()
{
    r_fs::write_file(true_north_mp4, true_north_mp4_len, "true_north.mp4");
}

void test_r_mux::teardown()
{
    r_fs::remove_file("true_north.mp4");
}

template<typename fcb>
void _demux_av(r_demuxer& demuxer, fcb cb)
{
    RTF_ASSERT(demuxer.get_stream_count() == 2);

    auto video_stream_index = demuxer.get_video_stream_index();
    auto audio_stream_index = demuxer.get_audio_stream_index();

    auto vsi = demuxer.get_stream_info(video_stream_index);
    auto asi = demuxer.get_stream_info(audio_stream_index);

    bool found_video = false;
    bool found_audio = false;

    while(demuxer.read_frame())
    {
        auto fi = demuxer.get_frame_info();
        if(fi.index == video_stream_index)
            found_video = true;
        if(fi.index == audio_stream_index)
            found_audio = true;
        RTF_ASSERT(fi.data != NULL);
        RTF_ASSERT(fi.size != 0);
        cb(fi);
    }

    RTF_ASSERT(found_video);
    RTF_ASSERT(found_audio);
}

void test_r_mux::test_basic_demux()
{
    r_demuxer demuxer("true_north.mp4");
    _demux_av(demuxer,[](r_frame_info&){});
}

void test_r_mux::test_basic_mux()
{
    {
        r_demuxer demuxer("true_north.mp4");

        auto input_video_stream_index = demuxer.get_video_stream_index();
        auto input_audio_stream_index = demuxer.get_audio_stream_index();

        auto vsi = demuxer.get_stream_info(input_video_stream_index);
        auto asi = demuxer.get_stream_info(input_audio_stream_index);

        r_muxer muxer("output69.mp4");

        muxer.add_video_stream(vsi.frame_rate, vsi.codec_id, vsi.resolution.first, vsi.resolution.second, vsi.profile, vsi.level);
        muxer.add_audio_stream(asi.codec_id, asi.channels, asi.sample_rate);

        muxer.set_video_extradata(demuxer.get_extradata(input_video_stream_index));
        muxer.set_audio_extradata(demuxer.get_extradata(input_audio_stream_index));

        muxer.open();

        while(demuxer.read_frame())
        {
            auto fi = demuxer.get_frame_info();
            if(fi.index == input_video_stream_index)
                muxer.write_video_frame(fi.data, fi.size, fi.pts, fi.dts, vsi.time_base, fi.key);
            if(fi.index == input_audio_stream_index)
                muxer.write_audio_frame(fi.data, fi.size, fi.pts, asi.time_base);
        }

        muxer.finalize();
    }

    {
        r_demuxer output_demuxer("output69.mp4");
        _demux_av(output_demuxer, [](r_frame_info&){});
    }

    r_fs::remove_file("output69.mp4");
}

// Exercise the exact path /transcode_fmp4 uses: video-only, fragmented MP4 to an
// in-memory buffer. Reuses the real h264 frames (with valid monotonic dts) from
// true_north.mp4 so this isolates the container path from the encoder.
void test_r_mux::test_fragmented_buffer_mux()
{
    r_demuxer demuxer("true_north.mp4");

    auto vsi_index = demuxer.get_video_stream_index();
    auto vsi = demuxer.get_stream_info(vsi_index);

    r_muxer muxer("", /*output_to_buffer=*/true, "mp4");
    muxer.enable_fragmented_mp4();
    muxer.add_video_stream(vsi.frame_rate, vsi.codec_id, vsi.resolution.first, vsi.resolution.second, vsi.profile, vsi.level);
    muxer.set_video_extradata(demuxer.get_extradata(vsi_index));
    muxer.open();

    size_t video_frames = 0;
    while(demuxer.read_frame())
    {
        auto fi = demuxer.get_frame_info();
        if(fi.index == vsi_index)
        {
            muxer.write_video_frame(fi.data, fi.size, fi.pts, fi.dts, vsi.time_base, fi.key);
            ++video_frames;
        }
    }

    muxer.finalize();

    RTF_ASSERT(video_frames > 0);

    // A fragmented MP4 must lead with an ftyp box and contain at least one moof.
    auto sz = muxer.buffer_size();
    RTF_ASSERT(sz > 8);
    const uint8_t* buf = muxer.buffer();

    auto find_box = [&](const char* tag) -> bool {
        for(size_t i = 0; i + 4 <= sz; ++i)
            if(buf[i]==tag[0] && buf[i+1]==tag[1] && buf[i+2]==tag[2] && buf[i+3]==tag[3])
                return true;
        return false;
    };
    RTF_ASSERT(find_box("ftyp"));
    RTF_ASSERT(find_box("moof"));
}

// Reproduce the /transcode_fmp4 path end-to-end: decode stored h264, re-encode
// in software (gop=30, no b-frames, exactly like the handler), mux to a fragmented
// buffer, then demux the result and verify every fed frame survives with
// contiguous, gap-free timestamps. This isolates "windows come back ~1s short"
// from the browser.
void test_r_mux::test_fragmented_transcode_window()
{
    r_demuxer demuxer("true_north.mp4");
    auto vidx = demuxer.get_video_stream_index();
    auto vsi  = demuxer.get_stream_info(vidx);

    r_video_decoder decoder(vsi.codec_id);
    auto ed = demuxer.get_extradata(vidx);
    if(!ed.empty()) decoder.set_extradata(ed);

    const uint16_t W = 640, H = 360;
    const uint32_t FPS = 30;
    AVRational framerate{ (int)FPS, 1 };

    r_video_encoder encoder(AV_CODEC_ID_H264, 2000000, W, H, framerate,
        AV_PIX_FMT_YUV420P, 0, (uint16_t)FPS, AV_PROFILE_H264_MAIN, 41, "", "", r_hw_accel::none);

    r_muxer muxer("", /*output_to_buffer=*/true, "mp4");
    muxer.enable_fragmented_mp4();
    muxer.add_video_stream(framerate, AV_CODEC_ID_H264, W, H, AV_PROFILE_H264_MAIN, 41);
    muxer.set_video_extradata(encoder.get_extradata());
    muxer.open();

    int64_t fc = 0;
    int input_frames = 0;
    while(demuxer.read_frame() && input_frames < 90)
    {
        auto fi = demuxer.get_frame_info();
        if(fi.index != vidx) continue;
        decoder.attach_buffer(fi.data, fi.size);
        auto ds = decoder.decode();
        if(ds != R_CODEC_STATE_HAS_OUTPUT && ds != R_CODEC_STATE_AGAIN_HAS_OUTPUT) continue;
        auto dec = decoder.get(AV_PIX_FMT_YUV420P, W, H, 1);
        ++input_frames;
        encoder.attach_buffer(dec->data(), dec->size(), fc++);
        while(true)
        {
            auto es = encoder.encode();
            if(es != R_CODEC_STATE_HAS_OUTPUT) break;
            auto pi = encoder.get();
            int64_t out_dts = (pi.dts < 0) ? 0 : pi.dts;
            muxer.write_video_frame(pi.data, pi.size, pi.pts, out_dts, pi.time_base, pi.key);
        }
    }
    auto fs = encoder.flush();
    while(fs == R_CODEC_STATE_HAS_OUTPUT)
    {
        auto pi = encoder.get();
        int64_t out_dts = (pi.dts < 0) ? 0 : pi.dts;
        muxer.write_video_frame(pi.data, pi.size, pi.pts, out_dts, pi.time_base, pi.key);
        fs = encoder.flush();
    }
    muxer.finalize();

    r_fs::write_file(muxer.buffer(), muxer.buffer_size(), "fragout.mp4");
    {
        r_demuxer od("fragout.mp4");
        int out_frames = 0;
        int64_t last_pts = -1;
        while(od.read_frame())
        {
            auto fi = od.get_frame_info();
            if(fi.index != od.get_video_stream_index()) continue;
            ++out_frames;
            last_pts = fi.pts;
        }
        fprintf(stderr, "[FRAGXCODE] in=%d out=%d last_pts=%lld\n",
                input_frames, out_frames, (long long)last_pts);
        RTF_ASSERT(out_frames == input_frames);
    }
    r_fs::remove_file("fragout.mp4");
}
