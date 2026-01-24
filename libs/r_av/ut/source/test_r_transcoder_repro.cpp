
#include "test_r_transcoder_repro.h"
#include "r_av/r_transcoder.h"
#include "r_av/r_video_encoder.h"
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
