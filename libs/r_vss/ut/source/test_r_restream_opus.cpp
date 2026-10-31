#include "test_r_restream_opus.h"
#include "r_vss/r_stream_keeper.h"
#include "r_pipeline/r_gst_source.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <string>
#include <thread>

using namespace std;
using namespace r_vss;
using namespace r_utils;
using r_pipeline::r_encoding;

REGISTER_TEST_FIXTURE(test_r_restream_opus);

void test_r_restream_opus::setup()
{
    r_pipeline::gstreamer_init();
}

void test_r_restream_opus::test_launch_strings()
{
    auto s = r_stream_keeper::create_restream_opus_launch_string(r_encoding::H264_ENCODING, 96, r_nullable<r_encoding>(r_encoding::AAC_GENERIC_ENCODING));
    // The camera's video as its own restream has it, then the audio decoded and encoded to Opus.
    RTF_ASSERT(s.find("( appsrc name=videosrc ! h264parse config-interval=-1 ! rtph264pay name=pay0 pt=96 ") == 0);
    RTF_ASSERT(s.find("appsrc name=audiosrc ! aacparse ! avdec_aac ! audioconvert ! audioresample ! audio/x-raw,rate=16000,channels=1 ! "
                      "opusenc bitrate=24000 frame-size=20 ! rtpopuspay name=pay1 pt=111 )") != string::npos);
    RTF_ASSERT(s.find("rtpmp4gpay") == string::npos);

    // G.711 is decoded too, and the Opus pt keeps clear of the video's.
    auto u = r_stream_keeper::create_restream_opus_launch_string(r_encoding::H265_ENCODING, 111, r_nullable<r_encoding>(r_encoding::PCMU_ENCODING));
    RTF_ASSERT(u.find("rtph265pay name=pay0 pt=111 ") != string::npos);
    RTF_ASSERT(u.find("appsrc name=audiosrc ! mulawdec ! ") != string::npos);
    RTF_ASSERT(u.find("rtpopuspay name=pay1 pt=110 )") != string::npos);
    RTF_ASSERT(r_stream_keeper::create_restream_opus_launch_string(r_encoding::H264_ENCODING, 96, r_nullable<r_encoding>(r_encoding::PCMA_ENCODING)).find("alawdec") != string::npos);

    // It parses as a bin.
    GError* err = nullptr;
    auto bin = gst_parse_launch(s.c_str(), &err);
    RTF_ASSERT(bin != nullptr && err == nullptr);
    if(bin)
        gst_object_unref(bin);
    if(err)
        g_error_free(err);
}

void test_r_restream_opus::test_no_opus_without_audio_it_decodes()
{
    RTF_ASSERT(r_stream_keeper::create_restream_opus_launch_string(r_encoding::H264_ENCODING, 96, r_nullable<r_encoding>()).empty());
    RTF_ASSERT(r_stream_keeper::create_restream_opus_launch_string(r_encoding::H264_ENCODING, 96, r_nullable<r_encoding>(r_encoding::AAC_LATM_ENCODING)).empty());
    RTF_ASSERT(restream_opus_audio_branch(r_encoding::H264_ENCODING).empty());

    // An element missing from GStreamer (rtpopuspay here, taken out of the
    // registry for a moment): no Opus restream rather than a broken one.
    auto registry = gst_registry_get();
    auto feature = gst_registry_lookup_feature(registry, "rtpopuspay");
    if(!feature)
        return;
    gst_registry_remove_feature(registry, feature);
    auto without = r_stream_keeper::create_restream_opus_launch_string(r_encoding::H264_ENCODING, 96, r_nullable<r_encoding>(r_encoding::AAC_GENERIC_ENCODING));
    gst_registry_add_feature(registry, feature);   // takes the lookup's reference
    RTF_ASSERT(without.empty());
    RTF_ASSERT(!restream_opus_audio_branch(r_encoding::AAC_GENERIC_ENCODING).empty());
}

// The branch run on real audio: Opus RTP comes out, at 16 kHz mono in 20 ms frames.
static int _opus_packets_from(const string& encoded_source)
{
    auto branch = restream_opus_audio_branch(r_encoding::AAC_GENERIC_ENCODING);
    if(encoded_source.find("mulawenc") != string::npos)
        branch = restream_opus_audio_branch(r_encoding::PCMU_ENCODING);
    if(branch.empty())
        return -1;
    auto launch = encoded_source + " ! " + branch + " ! appsink name=out sync=false";
    GError* err = nullptr;
    auto pipeline = gst_parse_launch(launch.c_str(), &err);
    if(!pipeline || err)
    {
        if(err)
            g_error_free(err);
        return -1;
    }
    auto out = gst_bin_get_by_name(GST_BIN(pipeline), "out");
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    int packets = 0;
    bool opus = false;
    while(auto sample = gst_app_sink_try_pull_sample(GST_APP_SINK(out), 5 * GST_SECOND))
    {
        auto caps = gst_sample_get_caps(sample);
        if(caps && !opus)
        {
            auto st = gst_caps_get_structure(caps, 0);
            auto name = gst_structure_get_string(st, "encoding-name");
            opus = name && string(name) == "OPUS";
        }
        ++packets;
        gst_sample_unref(sample);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(out);
    gst_object_unref(pipeline);
    return opus ? packets : -2;
}

static bool _have(std::initializer_list<const char*> elements)
{
    for(auto e : elements)
    {
        auto f = gst_element_factory_find(e);
        if(!f)
            return false;
        gst_object_unref(f);
    }
    return true;
}

void test_r_restream_opus::test_aac_becomes_opus()
{
    // Where GStreamer can't decode AAC or encode Opus there's no Opus restream (and
    // nothing to run here); avenc_aac only makes this test's input.
    if(restream_opus_audio_branch(r_encoding::AAC_GENERIC_ENCODING).empty() || !_have({"avenc_aac"}))
        return;
    // 2 s of tone at 16 kHz mono as AAC, as a camera sends it: about 100 Opus packets of 20 ms.
    auto n = _opus_packets_from("audiotestsrc num-buffers=32 samplesperbuffer=1000 ! audio/x-raw,rate=16000,channels=1 ! avenc_aac");
    RTF_ASSERT(n >= 90 && n <= 110);
}

void test_r_restream_opus::test_mulaw_becomes_opus()
{
    if(restream_opus_audio_branch(r_encoding::PCMU_ENCODING).empty() || !_have({"mulawenc"}))
        return;
    auto n = _opus_packets_from("audiotestsrc num-buffers=16 samplesperbuffer=1000 ! audio/x-raw,rate=8000,channels=1 ! mulawenc");
    RTF_ASSERT(n >= 90 && n <= 110);
}

// The mount as a viewer sees it: gst-rtsp-server describes the Opus restream
// (its appsrcs stood in for by test sources) with H.264 video and Opus audio.
void test_r_restream_opus::test_mount_describes_opus()
{
    auto launch = r_stream_keeper::create_restream_opus_launch_string(r_encoding::H264_ENCODING, 96, r_nullable<r_encoding>(r_encoding::AAC_GENERIC_ENCODING));
    if(launch.empty() || !_have({"x264enc", "avenc_aac"}))
        return;
    launch = r_string_utils::replace_all(launch, "appsrc name=videosrc",
        "videotestsrc is-live=true ! video/x-raw,width=320,height=240,framerate=10/1 ! x264enc tune=zerolatency key-int-max=10 ! video/x-h264,stream-format=byte-stream");
    launch = r_string_utils::replace_all(launch, "appsrc name=audiosrc",
        "audiotestsrc is-live=true ! audio/x-raw,rate=16000,channels=1 ! avenc_aac");

    int port = RTF_NEXT_PORT();
    auto server = gst_rtsp_server_new();
    gst_rtsp_server_set_address(server, "127.0.0.1");
    gst_rtsp_server_set_service(server, to_string(port).c_str());
    auto mounts = gst_rtsp_server_get_mount_points(server);
    auto factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, launch.c_str());
    gst_rtsp_mount_points_add_factory(mounts, "/cam/opus", factory);
    g_object_unref(mounts);

    auto ctx = g_main_context_new();
    auto loop = g_main_loop_new(ctx, FALSE);
    auto source_id = gst_rtsp_server_attach(server, ctx);
    RTF_ASSERT(source_id != 0);
    thread th([&](){ g_main_loop_run(loop); });

    string sdp;
    {
        r_socket sok;
        sok.connect("127.0.0.1", port);
        string url = "rtsp://127.0.0.1:" + to_string(port) + "/cam/opus";
        string req = "DESCRIBE " + url + " RTSP/1.0\r\nCSeq: 1\r\nAccept: application/sdp\r\n\r\n";
        r_networking::r_send(sok, req.data(), req.size(), 5000);
        string head;
        char c;
        while(head.size() < 16384 && head.find("\r\n\r\n") == string::npos && r_networking::r_recv(sok, &c, 1, 10000) == 1)
            head.push_back(c);
        size_t content_length = 0;
        for(auto& l : r_string_utils::split(head, "\r\n"))
            if(r_string_utils::to_lower(l).compare(0, 15, "content-length:") == 0)
                content_length = (size_t)r_string_utils::s_to_int(r_string_utils::strip(l.substr(15)));
        if(content_length > 0)
        {
            sdp.resize(content_length);
            r_networking::r_recv(sok, &sdp[0], content_length, 5000);
        }
        RTF_ASSERT(head.find("RTSP/1.0 200") == 0);
    }

    g_main_loop_quit(loop);
    th.join();
    if(auto src = g_main_context_find_source_by_id(ctx, source_id))
        g_source_destroy(src);
    g_main_loop_unref(loop);
    g_main_context_unref(ctx);
    g_object_unref(server);

    RTF_ASSERT(sdp.find("m=video") != string::npos && sdp.find("H264/90000") != string::npos);
    RTF_ASSERT(sdp.find("m=audio") != string::npos && sdp.find("OPUS/48000/2") != string::npos);
    RTF_ASSERT(sdp.find("MPEG4-GENERIC") == string::npos);
}
