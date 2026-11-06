
#include "test_r_transcoder_repro.h"
#include "r_av/r_transcoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_audio_encoder.h"
#include "r_av/r_audio_decoder.h"
#include "r_av/r_demuxer.h"
#include <cmath>
#include <cstdlib>
#include <sys/stat.h>
#include "r_utils/r_file.h"
#include <vector>
#include <cstring>
#include <thread>

using namespace std;
using namespace r_av;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_transcoder_repro);

void test_r_transcoder_repro::setup()
{
}

void test_r_transcoder_repro::teardown()
{
    //if(r_fs::file_exists("repro_output.ts"))
    //    r_fs::remove_file("repro_output.ts");
}

void test_r_transcoder_repro::test_4k_transcode()
{
    // 1. Setup 4K Encoder to generate input stream
    uint16_t input_width = 3840;
    uint16_t input_height = 2160;
    AVRational framerate = {30, 1};
    
    r_video_encoder source_encoder(
        AV_CODEC_ID_H264,
        4000000, // 4Mbps
        input_width,
        input_height,
        framerate,
        AV_PIX_FMT_YUV420P,
        0,
        30,
        AV_PROFILE_H264_MAIN,
        51,
        "veryfast",
        "zerolatency"
    );

    // 3. Generate frames
    size_t y_size = input_width * input_height;
    size_t uv_size = y_size / 4;
    size_t frame_size = y_size + 2 * uv_size;
    vector<uint8_t> raw_frame(frame_size);
    
    // Fill with dummy data (gray)
    memset(raw_frame.data(), 128, y_size);
    memset(raw_frame.data() + y_size, 128, uv_size);
    memset(raw_frame.data() + y_size + uv_size, 128, uv_size);

    // Encode one frame to ensure extradata is populated
    source_encoder.attach_buffer(raw_frame.data(), raw_frame.size(), 0);
    while(source_encoder.encode() == R_CODEC_STATE_HAS_OUTPUT)
    {
        source_encoder.get(); // Consume packet but ignore for now, we just want extradata
    }
    
    auto extradata = source_encoder.get_extradata();
    
    // Output parameters
    uint16_t output_width = 640;
    uint16_t output_height = 360;
    
    r_transcoder transcoder(
        "repro_output.ts",
        "mpegts",
        AV_CODEC_ID_H264,
        extradata,
        {1, 1000}, // Input timebase (ms)
        output_width,
        output_height,
        framerate,
        1000000,
        2000000,
        500000,
        false
    );

    transcoder.start();

    int num_frames = 30; // 1 second of video
    
    for(int i = 0; i < num_frames; ++i)
    {
        source_encoder.attach_buffer(raw_frame.data(), raw_frame.size(), i);
        while(true)
        {
            auto state = source_encoder.encode();
            if(state != R_CODEC_STATE_HAS_OUTPUT)
                break;

            auto pkt = source_encoder.get();
            int64_t pts_ms = (i * 1000) / 30;
            transcoder.write_frame(pkt.data, pkt.size, pts_ms);
        }
    }
    
    // Flush encoder
    while(source_encoder.flush() == R_CODEC_STATE_HAS_OUTPUT)
    {
        auto pkt = source_encoder.get();
        transcoder.write_frame(pkt.data, pkt.size, (num_frames * 1000) / 30);
    }

    // Allow some time for transcoder worker to process
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    transcoder.stop();

    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Verify output exists and has size
    RTF_ASSERT(r_fs::file_exists("repro_output.ts"));
        
    RTF_ASSERT(r_fs::file_size("repro_output.ts") > 0);
}

// A second of 320x240 gray H.264 at 15 fps, and AAC 16 kHz mono (a camera's audio, as Revere records it), written to
// a transcoder into MPEG-TS; `audio` says whether the transcoder is made with an audio stream.
static void transcode_with_audio(const string& path, bool audio, int& audio_packets_written)
{
    const uint16_t w = 320, h = 240;
    const AVRational fps = {15, 1};
    r_video_encoder video(AV_CODEC_ID_H264, 500000, w, h, fps, AV_PIX_FMT_YUV420P, 0, 15, AV_PROFILE_H264_MAIN, 41, "veryfast", "zerolatency");
    vector<uint8_t> gray(w * h * 3 / 2, 128);
    video.attach_buffer(gray.data(), gray.size(), 0);
    vector<pair<vector<uint8_t>, int64_t>> video_packets;
    while (video.encode() == R_CODEC_STATE_HAS_OUTPUT)
    {
        auto p = video.get();
        video_packets.push_back({vector<uint8_t>(p.data, p.data + p.size), 0});
    }
    for (int i = 1; i < 15; ++i)
    {
        video.attach_buffer(gray.data(), gray.size(), i);
        while (video.encode() == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto p = video.get();
            video_packets.push_back({vector<uint8_t>(p.data, p.data + p.size), (int64_t)i * 1000 / 15});
        }
    }

    const int rate = 16000;
    r_audio_encoder aac(AV_CODEC_ID_AAC, 64000, rate, 1, AV_SAMPLE_FMT_FLTP);
    int fs = aac.get_frame_size();
    vector<pair<vector<uint8_t>, int64_t>> audio_packets;
    for (int at = 0; at + fs <= rate; at += fs)
    {
        vector<float> f(fs);
        for (int i = 0; i < fs; ++i)
            f[i] = 0.25f * (float)sin(2 * M_PI * 1000.0 * (at + i) / rate);
        aac.attach_buffer(reinterpret_cast<const uint8_t*>(f.data()), f.size() * sizeof(float), at);
        if (aac.encode() == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto p = aac.get();
            audio_packets.push_back({vector<uint8_t>(p.data, p.data + p.size), (int64_t)at * 1000 / rate});
        }
    }

    optional<r_transcoder_audio> a;
    if (audio)
        a = r_transcoder_audio{AV_CODEC_ID_AAC, 1, (uint32_t)rate, aac.get_extradata()};
    {
        r_transcoder t(path, "mpegts", AV_CODEC_ID_H264, video.get_extradata(), {1, 1000}, w, h, fps,
                       500000, 1000000, 250000, false, "", a);
        RTF_ASSERT(t.has_audio() == audio);
        t.start();
        // By timestamp, as cloud_stream feeds them.
        size_t vi = 0, ai = 0;
        audio_packets_written = 0;
        while (vi < video_packets.size() || ai < audio_packets.size())
        {
            bool take_audio = ai < audio_packets.size() && (vi >= video_packets.size() || audio_packets[ai].second < video_packets[vi].second);
            if (take_audio)
            {
                t.write_audio_frame(audio_packets[ai].first.data(), audio_packets[ai].first.size(), audio_packets[ai].second);
                ++ai;
                ++audio_packets_written;
            }
            else
            {
                t.write_frame(video_packets[vi].first.data(), video_packets[vi].first.size(), video_packets[vi].second);
                ++vi;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        t.stop();
    }
}

// The video frames in a transcoder's output.
static int video_frames_in(const string& path)
{
    r_demuxer d(path);
    auto vi = d.get_video_stream_index();
    int n = 0;
    while (d.read_frame())
        if (d.get_frame_info().index == vi)
            ++n;
    return n;
}

void test_r_transcoder_repro::test_audio_written_as_it_comes()
{
    int written = 0;
    transcode_with_audio("transcoder_audio.ts", true, written);
    RTF_ASSERT(written > 10);

    // Scoped so the demuxer closes the file before it's removed (Windows can't
    // delete an open file).
    {
        r_demuxer d("transcoder_audio.ts");
        auto ai = d.get_audio_stream_index();
        RTF_ASSERT(ai >= 0);
        auto info = d.get_stream_info(ai);
        RTF_ASSERT(info.codec_id == AV_CODEC_ID_AAC);
        RTF_ASSERT(info.sample_rate == 16000);
        RTF_ASSERT(info.channels == 1);
        RTF_ASSERT(d.get_video_stream_index() >= 0);
        r_audio_decoder dec(AV_CODEC_ID_AAC);
        if (!info.extradata.empty())
            dec.set_extradata(info.extradata);
        int audio_read = 0, audio_decoded = 0, video_read = 0;
        int64_t first_pts = -1, last_pts = -1, first_video_pts = -1;
        bool pts_climbs = true;
        while (d.read_frame())
        {
            auto fi = d.get_frame_info();
            if (fi.index == ai)
            {
                ++audio_read;
                if (last_pts >= 0 && fi.pts <= last_pts)
                    pts_climbs = false;
                if (first_pts < 0)
                    first_pts = fi.pts;
                last_pts = fi.pts;
                dec.attach_buffer(fi.data, fi.size);
                auto st = dec.decode();
                if (st == R_CODEC_STATE_HAS_OUTPUT || st == R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                    ++audio_decoded;
            }
            else if (fi.index == d.get_video_stream_index())
            {
                if (first_video_pts < 0)
                    first_video_pts = fi.pts;
                ++video_read;
            }
        }
        // Every AAC frame written is in the output, beside the video, in order, and plays.
        RTF_ASSERT(audio_read == written);
        RTF_ASSERT(audio_decoded >= written - 2);
        RTF_ASSERT(pts_climbs);
        // About a second of audio, on the stream's clock.
        auto span_ms = av_rescale_q(last_pts - first_pts, info.time_base, {1, 1000});
        RTF_ASSERT(span_ms > 800 && span_ms < 1100);
        RTF_ASSERT(video_read > 0);
        // In step with the video: both were written from 0 ms, so they start together in the TS.
        auto vinfo = d.get_stream_info(d.get_video_stream_index());
        auto lead_ms = av_rescale_q(first_pts, info.time_base, {1, 1000}) - av_rescale_q(first_video_pts, vinfo.time_base, {1, 1000});
        RTF_ASSERT(lead_ms > -100 && lead_ms < 100);
    }
    r_fs::remove_file("transcoder_audio.ts");
}

void test_r_transcoder_repro::test_no_audio_without_it()
{
    int written = 0;
    transcode_with_audio("transcoder_no_audio.ts", false, written);
    {
        r_demuxer d("transcoder_no_audio.ts");
        // A transcoder made without audio ignores audio frames: video only, as before (no audio stream to find).
        bool no_audio = false;
        try { d.get_audio_stream_index(); } catch (const std::exception&) { no_audio = true; }
        RTF_ASSERT(no_audio);
        RTF_ASSERT(d.get_video_stream_index() >= 0);
    }

    // The same video with audio beside it: the audio takes nothing from the video (none of it goes to the decoder).
    transcode_with_audio("transcoder_with_audio.ts", true, written);
    RTF_ASSERT(video_frames_in("transcoder_with_audio.ts") == video_frames_in("transcoder_no_audio.ts"));
    r_fs::remove_file("transcoder_with_audio.ts");
    r_fs::remove_file("transcoder_no_audio.ts");

    // Audio with no sample rate (a camera whose rate wasn't recorded) is left out, not muxed broken.
    r_video_encoder video(AV_CODEC_ID_H264, 500000, 320, 240, {15, 1}, AV_PIX_FMT_YUV420P, 0, 15, AV_PROFILE_H264_MAIN, 41, "veryfast", "zerolatency");
    {
        r_transcoder t("transcoder_no_rate.ts", "mpegts", AV_CODEC_ID_H264, video.get_extradata(), {1, 1000}, 320, 240, {15, 1},
                       500000, 1000000, 250000, false, "", r_transcoder_audio{AV_CODEC_ID_AAC, 1, 0, {}});
        RTF_ASSERT(!t.has_audio());
    }
    r_fs::remove_file("transcoder_no_rate.ts");
}

void test_r_transcoder_repro::test_quiet_audio_does_not_hold_video()
{
    // Live, a camera's audio can pause. 0.2 s of audio and then 4 s of video: the video must reach the output while
    // the transcoder runs, not wait for FFmpeg's default 10 s interleave (R_TRANSCODER_AUDIO_INTERLEAVE_US).
    const uint16_t w = 320, h = 240;
    r_video_encoder video(AV_CODEC_ID_H264, 2000000, w, h, {15, 1}, AV_PIX_FMT_YUV420P, 0, 15, AV_PROFILE_H264_MAIN, 41, "veryfast", "zerolatency");
    vector<uint8_t> frame(w * h * 3 / 2, 128);
    vector<pair<vector<uint8_t>, int64_t>> vp;
    srand(7);
    for (int i = 0; i < 60; ++i)
    {
        // Noise, so 4 s is well past the output's write buffer.
        for (size_t k = 0; k < (size_t)w * h; ++k)
            frame[k] = (uint8_t)(rand() & 0xff);
        video.attach_buffer(frame.data(), frame.size(), i);
        while (video.encode() == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto p = video.get();
            vp.push_back({vector<uint8_t>(p.data, p.data + p.size), (int64_t)i * 1000 / 15});
        }
    }
    r_audio_encoder aac(AV_CODEC_ID_AAC, 64000, 16000, 1, AV_SAMPLE_FMT_FLTP);
    vector<pair<vector<uint8_t>, int64_t>> ap;
    for (int at = 0; at < 16000 / 5; at += aac.get_frame_size())
    {
        vector<float> f(aac.get_frame_size(), 0.1f);
        aac.attach_buffer(reinterpret_cast<const uint8_t*>(f.data()), f.size() * sizeof(float), at);
        if (aac.encode() == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto p = aac.get();
            ap.push_back({vector<uint8_t>(p.data, p.data + p.size), (int64_t)at * 1000 / 16000});
        }
    }

    const string path = "transcoder_quiet_audio.ts";
    long on_disk = 0;
    {
        r_transcoder t(path, "mpegts", AV_CODEC_ID_H264, video.get_extradata(), {1, 1000}, w, h, {15, 1},
                       4000000, 4000000, 4000000, false, "", r_transcoder_audio{AV_CODEC_ID_AAC, 1, 16000, aac.get_extradata()});
        t.start();
        size_t ai = 0;
        for (auto& v : vp)
        {
            while (ai < ap.size() && ap[ai].second <= v.second)
            {
                t.write_audio_frame(ap[ai].first.data(), ap[ai].first.size(), ap[ai].second);
                ++ai;
            }
            t.write_frame(v.first.data(), v.first.size(), v.second);
        }
        // Up to 10 s for the video to show up; held, nothing past the header is written until stop().
        for (int i = 0; i < 100 && on_disk < 256 * 1024; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            struct stat st;
            on_disk = stat(path.c_str(), &st) == 0 ? (long)st.st_size : 0;
        }
        t.stop();
    }
    RTF_ASSERT(on_disk >= 256 * 1024);
    r_fs::remove_file(path);
}
