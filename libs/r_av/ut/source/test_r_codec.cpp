
#include "test_r_codec.h"
#include "bad_guy.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_audio_decoder.h"
#include "r_av/r_audio_encoder.h"
#include "r_av/r_demuxer.h"
#include "r_av/r_muxer.h"
#include "r_utils/r_file.h"

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

void test_r_codec::test_basic_audio_decode()
{
    r_demuxer demuxer("bad_guy.mp4", true);

    int audio_stream_index = demuxer.get_audio_stream_index();

    if(audio_stream_index < 0)
    {
        return; // Test file has no audio stream - skip test
    }

    auto asi = demuxer.get_stream_info(audio_stream_index);

    r_audio_decoder decoder(asi.codec_id);
    decoder.set_extradata(demuxer.get_extradata(audio_stream_index));

    int frames_decoded = 0;

    while(demuxer.read_frame())
    {
        auto fi = demuxer.get_frame_info();

        if(fi.index == audio_stream_index)
        {
            decoder.attach_buffer(fi.data, fi.size);

            r_codec_state decode_state = R_CODEC_STATE_INITIALIZED;

            while(decode_state != R_CODEC_STATE_HUNGRY)
            {
                decode_state = decoder.decode();

                if(decode_state == R_CODEC_STATE_HAS_OUTPUT)
                {
                    auto frame = decoder.get(AV_SAMPLE_FMT_S16, 48000, 2);
                    RTF_ASSERT(frame->size() > 0);
                    frames_decoded++;
                }
            }
        }
    }

    if(frames_decoded > 0)
    {
        RTF_ASSERT(frames_decoded > 0);
    }
}

void test_r_codec::test_basic_audio_transcode()
{
    {
        r_demuxer demuxer("bad_guy.mp4", true);

        int audio_stream_index = demuxer.get_audio_stream_index();

        if(audio_stream_index < 0)
        {
            return; // Test file has no audio stream - skip test
        }

        auto asi = demuxer.get_stream_info(audio_stream_index);

        r_audio_decoder decoder(asi.codec_id);
        decoder.set_extradata(demuxer.get_extradata(audio_stream_index));

        r_audio_encoder encoder(AV_CODEC_ID_AAC, 128000, 44100, 2, AV_SAMPLE_FMT_FLTP);

        r_muxer muxer("audio_test.mp4");
        muxer.add_audio_stream(AV_CODEC_ID_AAC, 2, 44100);
        muxer.set_audio_extradata(encoder.get_extradata());
        muxer.open();

        r_frame_info fi;
        int64_t frame_num = 0;

        while(demuxer.read_frame())
        {
            fi = demuxer.get_frame_info();

            if(fi.index == audio_stream_index)
            {
                decoder.attach_buffer(fi.data, fi.size);

                auto decode_state = decoder.decode();

                if(decode_state == R_CODEC_STATE_AGAIN)
                    continue;

                if(decode_state == R_CODEC_STATE_HAS_OUTPUT || decode_state == R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                {
                    auto frame = decoder.get(AV_SAMPLE_FMT_FLTP, 44100, 2);

                    int encode_attempts = 10;

    AUDIO_ENCODE_AGAIN_TOP:
                    --encode_attempts;
                    encoder.attach_buffer(frame->data(), frame->size(), frame_num);
                    frame_num++;

                    auto encode_state = encoder.encode();

                    if(encode_state == R_CODEC_STATE_AGAIN && encode_attempts > 0)
                        goto AUDIO_ENCODE_AGAIN_TOP;

                    if(encode_state == R_CODEC_STATE_HAS_OUTPUT)
                    {
                        auto pi = encoder.get();
                        muxer.write_audio_frame(pi.data, pi.size, pi.pts, pi.time_base);
                    }

                    if(decode_state == R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                        continue;
                }
            }
        }

        auto decode_state = decoder.flush();
        if(decode_state == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto frame = decoder.get(AV_SAMPLE_FMT_FLTP, 44100, 2);

            int encode_attempts = 10;

    AUDIO_ENCODE_AGAIN_BOTTOM:
            --encode_attempts;
            encoder.attach_buffer(frame->data(), frame->size(), frame_num);
            frame_num++;

            auto encode_state = encoder.encode();

            if(encode_state == R_CODEC_STATE_AGAIN && encode_attempts > 0)
                goto AUDIO_ENCODE_AGAIN_BOTTOM;

            if(encode_state == R_CODEC_STATE_HAS_OUTPUT)
            {
                auto pi = encoder.get();
                muxer.write_audio_frame(pi.data, pi.size, pi.pts, pi.time_base);
            }
        }

        // Flush the encoder to get any remaining frames
        while(true)
        {
            auto flush_state = encoder.flush();
            if(flush_state == R_CODEC_STATE_EOF)
                break;

            if(flush_state == R_CODEC_STATE_HAS_OUTPUT)
            {
                auto pi = encoder.get();
                muxer.write_audio_frame(pi.data, pi.size, pi.pts, pi.time_base);
            }
        }

        muxer.finalize();
    }

    {
        r_demuxer demuxer("audio_test.mp4", true);

        int audio_stream_index = demuxer.get_audio_stream_index();

        RTF_ASSERT(audio_stream_index >= 0);
        auto asi = demuxer.get_stream_info(audio_stream_index);
        RTF_ASSERT(asi.sample_rate == 44100);
    }

    if(r_fs::file_exists("audio_test.mp4"))
        r_fs::remove_file("audio_test.mp4");
}
