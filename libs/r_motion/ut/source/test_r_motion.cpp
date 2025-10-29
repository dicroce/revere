
#include "test_r_motion.h"
#include "r_motion/utils.h"
#include "r_motion/r_motion_state.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_avg.h"
#include "r_av/r_demuxer.h"
#include "r_av/r_video_decoder.h"
#include <deque>
#include <numeric>
#include <cinttypes>

#include "serv.h"

using namespace std;
using namespace r_utils;
using namespace r_av;
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
    r_fs::write_file(serv_mp4, serv_mp4_len, "serv.mp4");
}

void test_r_motion::teardown()
{
    r_fs::remove_file("serv.mp4");
}

// NOTE: _write_gray8() helper function was removed as it used deprecated image processing functions

// NOTE: test_basic_utils() was removed as it tested deprecated image processing functions
// that have been replaced by OpenCV-based implementations in r_motion_state

void test_r_motion::test_motion_state()
{
    r_demuxer demuxer("serv.mp4", true);
    auto video_stream_index = demuxer.get_video_stream_index();
    auto vsi = demuxer.get_stream_info(video_stream_index);

    auto ed = demuxer.get_extradata(video_stream_index);

    r_video_decoder decoder(vsi.codec_id);
    if(!ed.empty())
        decoder.set_extradata(ed);

    r_motion_state ms;

    bool done_demuxing = false;

    bool nonZeroMotion = false;

    while(!done_demuxing)
    {
        done_demuxing = !demuxer.read_frame();
        auto fi = demuxer.get_frame_info();

AGAIN:
        if(fi.index == video_stream_index && fi.key)
        {
            r_codec_state cs = R_CODEC_STATE_INITIALIZED;

            decoder.attach_buffer(fi.data, fi.size);
            cs = decoder.decode();

            if(cs == R_CODEC_STATE_AGAIN || cs == R_CODEC_STATE_HUNGRY)
                goto AGAIN;

            if(cs == R_CODEC_STATE_HAS_OUTPUT || cs == R_CODEC_STATE_AGAIN_HAS_OUTPUT)
            {
                auto frame = decoder.get(AV_PIX_FMT_ARGB, vsi.resolution.first, vsi.resolution.second, 1);

                r_image img;
                img.type = R_MOTION_IMAGE_TYPE_ARGB;
                img.width = vsi.resolution.first;
                img.height = vsi.resolution.second;
                img.data = *frame;

                auto maybe_mi = ms.process(img);

                if(!maybe_mi.is_null())
                {
                    auto mi = maybe_mi.value();
                    if(mi.motion > 0)
                        nonZeroMotion = true;
                }
                
                if(cs == R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                    goto AGAIN;
            }
        }
    }

    RTF_ASSERT(nonZeroMotion);
}

void test_r_motion::test_adaptive_masking()
{
    // Test with aggressive masking parameters for quick validation
    r_motion_state ms(100, 0.3, 0.8, 10);  // low threshold, fast decay, short observation period
    
    // Create a test image with consistent motion in one area
    r_image test_img;
    test_img.type = R_MOTION_IMAGE_TYPE_ARGB;
    test_img.width = 320;
    test_img.height = 240;
    test_img.data.resize(320 * 240 * 4, 0);
    
    // Fill with a pattern that simulates continuous motion in a specific region
    for(int frame = 0; frame < 20; frame++)
    {
        // Add consistent motion in upper-left quadrant (simulating swaying tree)
        for(int y = 0; y < 120; y++)
        {
            for(int x = 0; x < 160; x++)
            {
                int idx = (y * 320 + x) * 4;
                // Create oscillating pattern
                uint8_t intensity = (frame % 2 == 0) ? 100 : 150;
                test_img.data[idx + 0] = intensity;     // B
                test_img.data[idx + 1] = intensity;     // G
                test_img.data[idx + 2] = intensity;     // R
                test_img.data[idx + 3] = 255;           // A
            }
        }
        
        // Add occasional motion in lower-right quadrant (simulating person)
        if(frame > 10 && frame < 15)
        {
            for(int y = 120; y < 240; y++)
            {
                for(int x = 160; x < 320; x++)
                {
                    int idx = (y * 320 + x) * 4;
                    test_img.data[idx + 0] = 200;  // B
                    test_img.data[idx + 1] = 200;  // G
                    test_img.data[idx + 2] = 200;  // R
                    test_img.data[idx + 3] = 255;  // A
                }
            }
        }
        
        auto maybe_mi = ms.process(test_img);
        
        if(!maybe_mi.is_null())
        {
            auto mi = maybe_mi.value();
            
            // After observation period, masking should become active
            if(frame >= 10)
            {
                RTF_ASSERT(mi.masking_active);
                
                // Should have some masked pixels (from continuous motion)
                if(mi.masked_pixels > 0)
                {
                    printf("Frame %d: Motion before mask: %" PRIu64 ", after mask: %" PRIu64 ", masked: %" PRIu64 "\n", 
                           frame, mi.motion_before_mask, mi.motion, mi.masked_pixels);
                }
            }
        }
    }
}
