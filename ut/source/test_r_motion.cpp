
#include "test_r_motion.h"
#include "r_motion/utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_avg.h"
#include "r_mux/r_demuxer.h"
#include "r_codec/r_video_decoder.h"
#include <deque>

// Added to the global namespace by test_r_mux.cpp, so extern'd here:
extern unsigned char bad_guy_mp4[];
extern unsigned int bad_guy_mp4_len;

using namespace std;
using namespace r_utils;
using namespace r_mux;
using namespace r_codec;
using namespace r_motion;

REGISTER_TEST_FIXTURE(test_r_motion);

std::string get_env(const string& name)
{
    std::string output;
#ifdef IS_WINDOWS
    char* s = nullptr;
    size_t len = 0;
    _dupenv_s(&s, &len, name.c_str());
    if(s)
    {
        output = string(s, len);
        free(s);
    }
#endif
#ifdef IS_LINUX
    char* env = getenv(name.c_str());
    if(env)
        output = string(env);
#endif
    return output;
}

void test_r_motion::setup()
{
    r_fs::write_file(bad_guy_mp4, bad_guy_mp4_len, "bad_guy.mp4");
}

void test_r_motion::teardown()
{
    r_fs::remove_file("bad_guy.mp4");
}

static shared_ptr<vector<uint8_t>> decode_h264_frame(AVCodecID codec_id, AVPixelFormat fmt, uint16_t w, uint16_t h, const uint8_t* data, size_t size)
{
    r_video_decoder decoder(codec_id);

    decoder.attach_buffer(data, size);

    shared_ptr<vector<uint8_t>> result;
    r_codec_state decoder_state = R_CODEC_STATE_AGAIN;
    while(decoder_state == R_CODEC_STATE_AGAIN)
        decoder_state = decoder.decode();
    decoder_state = decoder.flush();
    if(decoder_state == R_CODEC_STATE_HAS_OUTPUT)
        result = decoder.get(AV_PIX_FMT_ARGB,w,h);

    return result;
}

void test_r_motion::test_basic_utils()
{
#if 0
    auto fakey_root = get_env("FAKEY_ROOT");

    if(!fakey_root.empty())
    {
        auto path = fakey_root + r_utils::PATH_SLASH + "conv.mp4";
        r_demuxer demuxer(path, true);
        auto video_stream_index = demuxer.get_video_stream_index();
        auto vsi = demuxer.get_stream_info(video_stream_index);

        r_video_decoder decoder(vsi.codec_id);
        decoder.set_output_pixel_format(AV_PIX_FMT_ARGB);
        decoder.set_output_width(640);
        decoder.set_output_height(480);

        deque<r_image> avg_set;
        bool have_last = false;
        r_image last;

        r_exp_avg<uint64_t> avg(0, 1000);

        while(demuxer.read_frame())
        {
            auto fi = demuxer.get_frame_info();

            if(fi.index == video_stream_index)
            {
                if(fi.key)
                {
                    decoder.attach_buffer(fi.data, fi.size);

                    r_video_decoder_state decoder_state = decoder.decode();

                    if(decoder_state == R_VIDEO_DECODER_STATE_HAS_OUTPUT)
                    {
                        auto result = decoder.get();

                        r_image img;
                        img.type = R_MOTION_IMAGE_TYPE_ARGB;
                        img.width = 640;
                        img.height = 480;
                        img.data = result;

                        auto bw = argb_to_gray8(img);

                        if(have_last)
                        {
                            auto motion = gray8_compute_motion(last, bw);

                            auto val = avg.update(motion.first);

                            printf("motion: %lu, stddev: %lu\n", val, avg.standard_deviation());

//                            if(motion.first > 3200000000)
//                            {
//                                ppm_write_argb(r_string_utils::uint64_to_s(motion.first) + ".ppm", gray8_to_argb(motion.second));
//                            }

//                            printf("motion=%lu\n",motion.first);
                        }

                        if(!have_last)
                        {
                            last = bw;
                            have_last = true;
                        }

                        if(avg_set.size() > 10)
                            avg_set.pop_front();
                    }
                }
            }
        }
    }
#endif
#if 0
    r_demuxer demuxer("bad_guy.mp4", true);
    auto video_stream_index = demuxer.get_video_stream_index();
    auto vsi = demuxer.get_stream_info(video_stream_index);

    bool got_first_key_frame = false;
    vector<uint8_t> first_key_frame;

    bool got_second_key_frame = false;
    vector<uint8_t> second_key_frame;

    int nth_key_frame = 0;

    while(!got_first_key_frame || !got_second_key_frame)
    {
        demuxer.read_frame();
        auto fi = demuxer.get_frame_info();

        if(fi.index == video_stream_index)
        {
            if(fi.key)
            {
                ++nth_key_frame;
                if(nth_key_frame < 6)
                    continue;

                if(!got_first_key_frame)
                {
                    first_key_frame = decode_h264_frame(vsi.codec_id, AV_PIX_FMT_ARGB, fi.data, fi.size);
                    got_first_key_frame = true;
                }
                else if(!got_second_key_frame)
                {
                    second_key_frame = decode_h264_frame(vsi.codec_id, AV_PIX_FMT_ARGB, fi.data, fi.size);
                    got_second_key_frame = true;
                }
            }
        }
    }

    r_image first_img;
    first_img.type = R_MOTION_IMAGE_TYPE_ARGB;
    first_img.width = vsi.resolution.first;
    first_img.height = vsi.resolution.second;
    first_img.data = first_key_frame;

    r_image second_img;
    second_img.type = R_MOTION_IMAGE_TYPE_ARGB;
    second_img.width = vsi.resolution.first;
    second_img.height = vsi.resolution.second;
    second_img.data = second_key_frame;

    ppm_write_argb("first.ppm", first_img);
    ppm_write_argb("second.ppm", second_img);

    auto sub_result = gray8_compute_motion(argb_to_gray8(first_img), argb_to_gray8(second_img));

    printf("motion: %lu\n", sub_result.first);

//    auto output = gray16_to_argb(argb_to_gray16(img));

    ppm_write_argb("motion.ppm", gray8_to_argb(sub_result.second));
#endif
}
