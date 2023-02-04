
#include "test_r_mux.h"
#include "r_mux/r_demuxer.h"
#include "r_mux/r_muxer.h"
#include "r_utils/r_file.h"

#include "true_north.h"

using namespace std;
using namespace std::chrono;
using namespace r_utils;
using namespace r_mux;

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
    _demux_av(demuxer,[](r_frame_info& fi){});
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
        _demux_av(output_demuxer, [](r_frame_info& fi){});
    }

    r_fs::remove_file("output69.mp4");
}
