
#include "test_r_vss.h"
#include "utils.h"
#include "r_vss/r_stream_keeper.h"
#include "r_storage/r_storage_file.h"
#include "r_storage/r_storage_file_reader.h"
#include "r_disco/r_stream_config.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_blob_tree.h"
#include "r_pipeline/r_stream_info.h"
#include "r_mux/r_muxer.h"
#include "r_mux/r_demuxer.h"
#include <gst/gst.h>

using namespace std;
using namespace r_storage;
using namespace r_disco;
using namespace r_utils;
using namespace r_vss;
using namespace r_pipeline;
using namespace r_mux;

REGISTER_TEST_FIXTURE(test_r_vss);

static void _whack_files()
{
    if(r_fs::file_exists("top_dir/video/ten_mb_file"))
        r_fs::remove_file("top_dir/video/ten_mb_file");
    if(r_fs::file_exists("top_dir/video"))
        r_fs::rmdir("top_dir/video");

    if(r_fs::file_exists("top_dir/db/cameras.db"))
        r_fs::remove_file("top_dir/db/cameras.db");
    if(r_fs::file_exists("top_dir/db"))
        r_fs::rmdir("top_dir/db");

    if(r_fs::file_exists("top_dir/config"))
        r_fs::rmdir("top_dir/config");

    if(r_fs::file_exists("top_dir"))
        r_fs::rmdir("top_dir");

    if(r_fs::file_exists("output_true_north.mov"))
        r_fs::remove_file("output_true_north.mov");
}

void test_r_vss::setup()
{
    gst_init(NULL, NULL);
    _whack_files();

    r_fs::mkdir("top_dir");
    r_fs::mkdir("top_dir" + PATH_SLASH + "config");
    r_fs::mkdir("top_dir" + PATH_SLASH + "db");
    r_fs::mkdir("top_dir" + PATH_SLASH + "video");
}

void test_r_vss::teardown()
{
    _whack_files();
}

void test_r_vss::test_r_stream_keeper_basic_recording()
{
    // First create a storage file for holding our video...
    string storage_file_path = "ten_mb_file";

    r_storage_file::allocate("top_dir" + PATH_SLASH + "video" + PATH_SLASH + storage_file_path, 65536, 160);

    // Then startup a fake camera...
    int port = RTF_NEXT_PORT();
    auto fc = _create_fc(port);

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    // Create a devices object so we can create a stream_keeper....
    r_devices devices("top_dir");
    devices.start();

    r_stream_keeper sk(devices, "top_dir");
    sk.start();

    // Create an example stream config for a stream our fake camera can stream...
    vector<pair<r_stream_config, string>> configs;

    r_stream_config cfg;
    cfg.id = "9d807570-3d0e-4f87-9773-ae8d6471eab6";
    cfg.ipv4 = "127.0.0.1";
    cfg.rtsp_url = r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h264_aac.mp4", port);
    cfg.video_codec = "h264";
    cfg.video_codec_parameters = "control=stream=0, framerate=23.976023976023978, mediaclk=sender, packetization-mode=1, profile-level-id=64000a, sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA, ts-refclk=local";
    cfg.video_timebase = 90000;
    cfg.audio_codec = "mpeg4-generic";
    cfg.audio_timebase = 48000;

    configs.push_back(make_pair(cfg, hash_stream_config(cfg)));

    // Adding a stream to our devices....

    devices.insert_or_update_devices(configs);

    // Setting our fake stream to "assigned" makes stream_keeper start recording...
    auto c = devices.get_camera_by_id("9d807570-3d0e-4f87-9773-ae8d6471eab6").value();

    c.record_file_path = storage_file_path;
    c.record_file_block_size = 65536;
    c.n_record_file_blocks = 160;

    devices.assign_camera(c);

    // Recording for 10 seconds should guarantee we actually get some audio and video.
    std::this_thread::sleep_for(std::chrono::seconds(15));

    RTF_ASSERT(sk.fetch_stream_status().size() == 1);

    sk.stop();

    // Create a storage file object so we can query from it...
    r_storage_file_reader sfr("top_dir" + PATH_SLASH + "video" + PATH_SLASH + storage_file_path);

    auto kfst = sfr.key_frame_start_times(R_STORAGE_MEDIA_TYPE_ALL);

    auto result = sfr.query(R_STORAGE_MEDIA_TYPE_ALL, kfst.front(), kfst.back());

    uint32_t version = 0;
    auto bt = r_blob_tree::deserialize(&result[0], result.size(), version);

    auto video_codec_name = bt["video_codec_name"].get_string();
    auto video_codec_parameters = bt["video_codec_parameters"].get_string();
    auto audio_codec_name = bt["audio_codec_name"].get_string();

    auto sps = get_h264_sps(bt["video_codec_parameters"].get_string());
    auto pps = get_h264_pps(bt["video_codec_parameters"].get_string());

    auto sps_info = parse_h264_sps(sps.value());

    // create an output mp4 file for our audio + video

    r_muxer muxer("output_true_north.mov");

    muxer.add_video_stream(
        av_d2q(23.9760, 10000),
        r_mux::encoding_to_av_codec_id(video_codec_name),
        sps_info.width,
        sps_info.height,
        sps_info.profile_idc,
        sps_info.level_idc
    );

    muxer.add_audio_stream(
        r_mux::encoding_to_av_codec_id(audio_codec_name),
        1,
        48000
    );

    muxer.set_video_extradata(make_h264_extradata(sps, pps));

    muxer.open();
    
    auto n_frames = bt["frames"].size();

    int64_t dts = 0;
    for(size_t fi = 0; fi < n_frames; ++fi)
    {
        auto sid = bt["frames"][fi]["stream_id"].get_string();
        auto stream_id = bt["frames"][fi]["stream_id"].get_value<int>();
        auto key = (bt["frames"][fi]["key"].get_string() == "true");
        auto frame = bt["frames"][fi]["data"].get();
        auto pts = bt["frames"][fi]["ts"].get_value<int64_t>();

        if(stream_id == R_STORAGE_MEDIA_TYPE_VIDEO)
        {
            muxer.write_video_frame(frame.data(), frame.size(), pts, dts, {1, 1000}, key);
        }
        else
        {
            muxer.write_audio_frame(frame.data(), frame.size(), pts, {1, 1000});
        }
    }

    muxer.finalize();

    // Finally, use a demuxer to verify our output file...

    r_demuxer demuxer("output_true_north.mov");

    RTF_ASSERT(demuxer.get_stream_count() == 2);

    auto video_stream_index = demuxer.get_video_stream_index();
    auto audio_stream_index = demuxer.get_audio_stream_index();

    bool foundVideo = false, foundAudio = false;

    while(demuxer.read_frame())
    {
        auto fi = demuxer.get_frame_info();
        if(fi.index == video_stream_index)
            foundVideo = true;
        else if(fi.index == audio_stream_index)
            foundAudio = true;
    }

    RTF_ASSERT(foundVideo);
    RTF_ASSERT(foundAudio);
}
