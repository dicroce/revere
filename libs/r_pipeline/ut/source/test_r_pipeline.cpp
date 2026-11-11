
#include "test_r_pipeline.h"
#include "utils.h"
#include "bad_guy.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_arg.h"
#include "r_pipeline/r_stream_info.h"
#include "r_fakey/r_fake_camera.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_work_q.h"
#include "r_utils/r_std_utils.h"
#include "r_utils/r_socket.h"

#include <sys/types.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <functional>
#include <algorithm>
#include <iterator>
#include <chrono>
#ifdef IS_WINDOWS
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif
#include <gst/gst.h>
#ifdef IS_WINDOWS
#pragma warning( pop )
#endif

using namespace std;
using namespace std::chrono;
using namespace r_pipeline;
using namespace r_fakey;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_pipeline);

void test_r_pipeline::setup()
{
    r_pipeline::gstreamer_init();
}

void test_r_pipeline::teardown()
{
}

void test_r_pipeline::test_stream_info_get_video_codec_framerate()
{
    // As build_video_codec_params writes it.
    auto fr = get_video_codec_framerate("sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA,sc_framerate=15.000000");
    RTF_ASSERT(!fr.is_null());
    RTF_ASSERT(fr.value() == 15.0f);

    // As the recording context writes it (", " separators, other attributes around it).
    fr = get_video_codec_framerate("packetization-mode=1, encoded_video_sample_caps=dmlkZW8=, sc_framerate=29.97, sprop-parameter-sets=Z2QACqzZ,aOvjyyLA");
    RTF_ASSERT(!fr.is_null());
    RTF_ASSERT(fr.value() > 29.96f && fr.value() < 29.98f);

    // Missing, empty, non-numeric or non-positive: null.
    RTF_ASSERT(get_video_codec_framerate("sprop-parameter-sets=Z2QACqzZ,aOvjyyLA").is_null());
    RTF_ASSERT(get_video_codec_framerate("").is_null());
    RTF_ASSERT(get_video_codec_framerate("sc_framerate=abc").is_null());
    RTF_ASSERT(get_video_codec_framerate("sc_framerate=0").is_null());
    RTF_ASSERT(get_video_codec_framerate("sc_framerate=").is_null());
}

void test_r_pipeline::test_gst_source_h264_aac()
{
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    std::thread loopThread([&] {
        g_main_loop_run(loop);
    });

    printf("test_gst_source_h264_aac\n");
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    vector<r_arg> arguments;
    add_argument(arguments, "url", r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h264_aac_no_bf.mp4", port));

    bool got_audio = false, got_video = false;

    r_gst_source src;
    src.set_args(arguments);
    src.set_video_sample_cb(
        [&](const sample_context&, const r_gst_buffer&, bool, int64_t){
            got_video = true;
        }
    );

    uint8_t audio_channels;
    uint32_t audio_sample_rate;
    src.set_audio_sample_cb(
        [&](const sample_context& ctx, const r_gst_buffer&, bool, int64_t){
            got_audio = true;
            audio_channels = ctx.audio_channels().value();
            audio_sample_rate = ctx.audio_sample_rate().value();
        }
    );

    bool got_ready = false;
    src.set_ready_cb([&](){
        got_ready = true;
    });

    src.play();

    // No "protocols" argument: rtspsrc gets TCP alone, never UDP first
    RTF_ASSERT(src.protocol_flags() == 0x00000004);

    std::this_thread::sleep_for(std::chrono::seconds(5));

    g_main_loop_quit(loop);
    loopThread.join();
    g_main_loop_unref(loop);

    src.stop();

    fc->quit();

    RTF_ASSERT(got_ready && got_audio && got_video);

    gst_deinit();
}

void test_r_pipeline::test_rtsp_protocol_flags()
{
    // GstRTSPLowerTrans: UDP 0x1, TCP 0x4, TLS 0x20
    RTF_ASSERT(string(RTSP_DEFAULT_PROTOCOLS) == "TCP");
    RTF_ASSERT(rtsp_protocol_flags(RTSP_DEFAULT_PROTOCOLS) == 0x00000004);
    RTF_ASSERT(rtsp_protocol_flags("UDP") == 0x00000001);
    RTF_ASSERT(rtsp_protocol_flags("tcp,udp") == 0x00000005);
    RTF_ASSERT(rtsp_protocol_flags("TCP,UDP,TLS") == 0x00000025);
    RTF_ASSERT(rtsp_protocol_flags("bogus") == 0);
}

void test_r_pipeline::test_rtsp_jitterbuffer_latency()
{
    gst_init(nullptr, nullptr);
    GstElement* rtspsrc = gst_element_factory_make("rtspsrc", nullptr);
    RTF_ASSERT(rtspsrc != nullptr);
    guint latency = 0;
    g_object_get(G_OBJECT(rtspsrc), "latency", &latency, NULL);
    RTF_ASSERT(latency == 2000); // GStreamer's default, what every camera had
    rtsp_set_jitterbuffer_latency(rtspsrc);
    g_object_get(G_OBJECT(rtspsrc), "latency", &latency, NULL);
    RTF_ASSERT(latency == (guint)RTSP_JITTERBUFFER_LATENCY_MS && latency == 200);
    gst_object_unref(rtspsrc);
}

void test_r_pipeline::test_rtsp_teardown_timeout()
{
    gst_init(nullptr, nullptr);
    GstElement* rtspsrc = gst_element_factory_make("rtspsrc", nullptr);
    RTF_ASSERT(rtspsrc != nullptr);
    guint64 timeout = 0;
    g_object_get(G_OBJECT(rtspsrc), "teardown-timeout", &timeout, NULL);
    RTF_ASSERT(timeout == 100000000ULL); // GStreamer's default: 100 ms to get the TEARDOWN out
    rtsp_set_teardown_timeout(rtspsrc);
    g_object_get(G_OBJECT(rtspsrc), "teardown-timeout", &timeout, NULL);
    RTF_ASSERT(timeout == RTSP_TEARDOWN_TIMEOUT_NS && timeout == 2000000000ULL);
    gst_object_unref(rtspsrc);
}

// A camera that answers over RTSP/TCP but sends no video, as one with its sessions stuck did. It writes down each
// request's method and session.
namespace
{
struct fake_silent_camera
{
    int port;
    std::mutex lock;
    vector<string> requests;
    std::atomic<bool> listening{false};
    std::atomic<bool> done{false};
    std::thread th;

    explicit fake_silent_camera(int p) : port(p)
    {
        th = std::thread([this](){ _serve(); });
        while(!listening)
            std::this_thread::yield();
    }
    ~fake_silent_camera() { done = true; if(th.joinable()) th.join(); }

    bool saw(const string& method)
    {
        std::lock_guard<std::mutex> g(lock);
        return std::any_of(requests.begin(), requests.end(), [&](const string& r){ return r.rfind(method, 0) == 0; });
    }

    void _serve()
    {
        r_socket listener;
        listener.bind(port, "127.0.0.1");
        listener.listen();
        listening = true;
        uint64_t wait = 10000;
        if(!listener.wait_till_recv_wont_block(wait))
            return;
        auto conn = listener.accept();
        string buf;
        const string sdp = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=cam\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\n"
                           "m=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\na=fmtp:96 packetization-mode=1\r\na=control:track1\r\n";
        while(!done)
        {
            uint64_t millis = 200;
            if(!conn.wait_till_recv_wont_block(millis))
                continue;
            char chunk[4096];
            int n = conn.recv(chunk, sizeof(chunk));
            if(n <= 0)
                return;
            buf.append(chunk, n);
            size_t end;
            while((end = buf.find("\r\n\r\n")) != string::npos)
            {
                string head = buf.substr(0, end);
                buf.erase(0, end + 4);
                string method = head.substr(0, head.find(' '));
                auto header = [&](const string& name){
                    auto at = head.find("\r\n" + name + ":");
                    if(at == string::npos) return string();
                    at += name.size() + 3;
                    auto stop = head.find("\r\n", at);
                    return r_string_utils::strip(head.substr(at, stop == string::npos ? string::npos : stop - at));
                };
                {
                    std::lock_guard<std::mutex> g(lock);
                    requests.push_back(method + " " + header("Session"));
                }
                string extra, body;
                if(method == "OPTIONS")
                    extra = "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n";
                else if(method == "DESCRIBE")
                {
                    body = sdp;
                    extra = r_string_utils::format("Content-Type: application/sdp\r\nContent-Base: rtsp://127.0.0.1:%d/x/\r\n", port);
                }
                else if(method == "SETUP")
                    extra = "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\nSession: S1;timeout=60\r\n";
                else
                    extra = "Session: S1\r\n";
                auto reply = r_string_utils::format("RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sContent-Length: %d\r\n\r\n",
                                                    header("CSeq").c_str(), extra.c_str(), (int)body.size()) + body;
                conn.send(reply.data(), reply.size());
            }
        }
    }
};
}

void test_r_pipeline::test_gst_source_stop_sends_teardown()
{
    // Stopping a stream tells the camera to end its session. A camera never told may hold the session, and some
    // refuse new ones until they're power cycled: TCP connects, then nothing.
    int port = RTF_NEXT_PORT();
    fake_silent_camera cam(port);

    vector<r_arg> arguments;
    add_argument(arguments, "url", r_string_utils::format("rtsp://127.0.0.1:%d/x", port));
    r_gst_source src;
    src.set_args(arguments);
    src.play();
    // 2 s to get the TEARDOWN out, not rtspsrc's 100 ms
    RTF_ASSERT(src.teardown_timeout_ns() == RTSP_TEARDOWN_TIMEOUT_NS);

    auto deadline = steady_clock::now() + seconds(10);
    while(!cam.saw("PLAY") && steady_clock::now() < deadline)
        std::this_thread::sleep_for(milliseconds(20));
    RTF_ASSERT(cam.saw("PLAY S1"));

    src.stop();

    deadline = steady_clock::now() + seconds(5);
    while(!cam.saw("TEARDOWN") && steady_clock::now() < deadline)
        std::this_thread::sleep_for(milliseconds(20));
    RTF_ASSERT(cam.saw("TEARDOWN S1"));
}

void test_r_pipeline::test_rtsp_probe_retry_over_udp()
{
    // Answered over TCP but no video: the camera won't stream over TCP
    RTF_ASSERT(rtsp_probe_retry_over_udp(true, false));
    // Video came: TCP works
    RTF_ASSERT(!rtsp_probe_retry_over_udp(true, true));
    // No answer: unreachable or wrong URL, UDP won't help
    RTF_ASSERT(!rtsp_probe_retry_over_udp(false, false));
}

#if 0
void test_r_pipeline::test_gst_source_h265_aac()
{

    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    vector<r_arg> arguments;
    add_argument(arguments, "url", r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h265_aac.mp4", port));

    bool got_audio = false, got_video = false;

    r_gst_source src;
    src.set_args(arguments);
    src.set_video_sample_cb(
        [&](const sample_context& ctx, const r_gst_buffer& buffer, bool key, int64_t pts){
            got_video = true;
        }
    );
    src.set_audio_sample_cb(
        [&](const sample_context& ctx, const r_gst_buffer& buffer, bool key, int64_t pts){
            got_audio = true;
        }
    );

    bool got_ready = false;
    src.set_ready_cb([&](){
        got_ready = true;
    });

    src.play();

    std::this_thread::sleep_for(std::chrono::seconds(5));

    src.stop();

    fc->quit();

    RTF_ASSERT(got_ready && got_audio && got_video);
}

void test_r_pipeline::test_gst_source_h264_mulaw()
{
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    vector<r_arg> arguments;
    add_argument(arguments, "url", r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h264_mulaw.mkv", port));

    bool got_audio = false, got_video = false;

    r_gst_source src;
    src.set_args(arguments);
    src.set_video_sample_cb(
        [&](const sample_context& ctx, const r_gst_buffer& buffer, bool key, int64_t pts){
            got_video = true;
        }
    );
    src.set_audio_sample_cb(
        [&](const sample_context& ctx, const r_gst_buffer& buffer, bool key, int64_t pts){
            got_audio = true;
        }
    );
    bool got_ready = false;
    src.set_ready_cb([&](){
        got_ready = true;
    });

    src.play();

    std::this_thread::sleep_for(std::chrono::seconds(5));

    src.stop();

    fc->quit();

    RTF_ASSERT(got_ready && got_audio && got_video);
}

void test_r_pipeline::test_gst_source_h265_mulaw()
{
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    vector<r_arg> arguments;
    add_argument(arguments, "url", r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h265_mulaw.mkv", port));

    bool got_audio = false, got_video = false;

    r_gst_source src;
    src.set_args(arguments);
    src.set_video_sample_cb(
        [&](const sample_context& ctx, const r_gst_buffer& buffer, bool key, int64_t pts){
            got_video = true;
        }
    );
    src.set_audio_sample_cb(
        [&](const sample_context& ctx, const r_gst_buffer& buffer, bool key, int64_t pts){
            got_audio = true;
        }
    );
    bool got_ready = false;
    src.set_ready_cb([&](){
        got_ready = true;
    });

    src.play();

    std::this_thread::sleep_for(std::chrono::seconds(5));

    src.stop();

    fc->quit();

    RTF_ASSERT(got_ready && got_audio && got_video);
}

void test_r_pipeline::test_gst_source_fetch_sdp()
{
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });

    {
        auto medias = fetch_sdp_media(r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h264_aac.mkv", port));

        RTF_ASSERT(medias["video"].rtpmaps[96].encoding == H264_ENCODING);
        RTF_ASSERT(medias["video"].rtpmaps[96].time_base == 90000);
        RTF_ASSERT(medias["audio"].rtpmaps[97].encoding == AAC_LATM_ENCODING);
    }

    {
        auto medias = fetch_sdp_media(r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h265_aac.mkv", port));

        RTF_ASSERT(medias["video"].rtpmaps[96].encoding == H265_ENCODING);
        RTF_ASSERT(medias["video"].rtpmaps[96].time_base == 90000);
        RTF_ASSERT(medias["audio"].rtpmaps[97].encoding == AAC_LATM_ENCODING);
    }

    fc->quit();
    fct.join();

}

void test_r_pipeline::test_gst_source_pull_real()
{
    vector<r_arg> arguments;
    add_argument(arguments, "url", "rtsp://192.168.1.20/h265Preview_01_main");
    //add_argument(arguments, "url", "rtsp://192.168.1.20/h264Preview_01_sub");

    add_argument(arguments, "username", "admin");
    add_argument(arguments, "password", "emperor1");

    r_gst_source src;
    src.set_args(arguments);

    map<string, r_sdp_media> medias;
    src.set_sdp_media_cb([&](const map<string, r_sdp_media>& sdp_medias){
        medias = sdp_medias;
        for(auto ap : medias["video"].rtpmaps)
            printf("%d: %d\n", ap.first, ap.second.time_base);
    });

    src.set_video_sample_cb(
        [&](const sample_context& ctx, const r_gst_buffer& buffer, bool key, int64_t pts){
            auto mi = buffer.map(r_gst_buffer::MT_READ);
        }
    );

    src.play();

    std::this_thread::sleep_for(std::chrono::seconds(10));

    src.stop();
}

void test_r_pipeline::test_gst_source_bframes()
{
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    vector<r_arg> arguments;
    add_argument(arguments, "url", r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h264_aac.mkv", port));

    r_gst_source src;
    src.set_args(arguments);
    src.set_video_sample_cb(
        [&](const sample_context& ctx, const r_gst_buffer& buffer, bool key, int64_t pts){
            //printf("bframes ts = %s, pts = %s, key = %s, size = %s\n", r_string_utils::int64_to_s(ts).c_str(), r_string_utils::int64_to_s(pts).c_str(), (key)?"true":"false", r_string_utils::size_t_to_s(sz).c_str());
        }
    );

    src.play();

    std::this_thread::sleep_for(std::chrono::seconds(10));

    src.stop();

    fc->quit();
}

void test_r_pipeline::test_gst_source_fetch_bytes_per_second()
{
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    auto bytes_per_second = fetch_bytes_per_second(r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h264_aac.mkv", port), 15);

    fc->quit();

    RTF_ASSERT(bytes_per_second >= 16000 && bytes_per_second <= 24000);
}

void test_r_pipeline::test_stream_info_get_info_frames()
{
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    auto medias = fetch_sdp_media(r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h264_aac.mkv", port));

    string video_codec_name, h264_codec_parameters;
    int video_timebase;
    tie(video_codec_name, h264_codec_parameters, video_timebase) = sdp_media_map_to_s(VIDEO_MEDIA, medias);

    auto h264_sps_b = get_h264_sps(h264_codec_parameters);
    RTF_ASSERT(!h264_sps_b.value().empty());
    auto h264_pps_b = get_h264_sps(h264_codec_parameters);
    RTF_ASSERT(!h264_pps_b.value().empty());

    medias = fetch_sdp_media(r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h265_aac.mkv", port));

    string h265_codec_parameters;
    tie(video_codec_name, h265_codec_parameters, video_timebase) = sdp_media_map_to_s(VIDEO_MEDIA, medias);

    auto h265_vps_b = get_h265_vps(h265_codec_parameters);
    RTF_ASSERT(!h265_vps_b.value().empty());
    auto h265_sps_b = get_h265_sps(h265_codec_parameters);
    RTF_ASSERT(!h265_sps_b.value().empty());
    auto h265_pps_b = get_h265_pps(h265_codec_parameters);
    RTF_ASSERT(!h265_pps_b.value().empty());

    fc->quit();
}

void test_r_pipeline::test_stream_info_make_extradata()
{
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    auto medias = fetch_sdp_media(r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h264_aac.mkv", port));

    string video_codec_name, h264_codec_parameters;
    int video_timebase;
    tie(video_codec_name, h264_codec_parameters, video_timebase) = sdp_media_map_to_s(VIDEO_MEDIA, medias);

    auto h264_sps_b = get_h264_sps(h264_codec_parameters);
    auto h264_pps_b = get_h264_pps(h264_codec_parameters);

    auto h264_ed = make_h264_extradata(h264_sps_b, h264_pps_b);
    RTF_ASSERT(!h264_ed.empty());

    medias = fetch_sdp_media(r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h265_aac.mkv", port));

    string h265_codec_parameters;
    tie(video_codec_name, h265_codec_parameters, video_timebase) = sdp_media_map_to_s(VIDEO_MEDIA, medias);

    auto h265_vps_b = get_h265_vps(h265_codec_parameters);
    auto h265_sps_b = get_h265_sps(h265_codec_parameters);
    auto h265_pps_b = get_h265_pps(h265_codec_parameters);

    auto h265_ed = make_h265_extradata(h265_vps_b, h265_sps_b, h265_pps_b);
    RTF_ASSERT(!h265_ed.empty());

    fc->quit();
}

void test_r_pipeline::test_stream_info_parse_h264_sps()
{
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    auto medias = fetch_sdp_media(r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h264_aac.mkv", port));

    string video_codec_name, h264_codec_parameters;
    int video_timebase;
    tie(video_codec_name, h264_codec_parameters, video_timebase) = sdp_media_map_to_s(VIDEO_MEDIA, medias);

    auto h264_sps_b = get_h264_sps(h264_codec_parameters);

    auto sps_info = parse_h264_sps(h264_sps_b.value());

    RTF_ASSERT(sps_info.width == 96);
    RTF_ASSERT(sps_info.height = 64);
    RTF_ASSERT(sps_info.profile_idc == 100);
    RTF_ASSERT(sps_info.level_idc == 10);

    fc->quit();
}

void test_r_pipeline::test_stream_info_parse_h265_sps()
{
    int port = RTF_NEXT_PORT();

    auto fc = _create_fc(port);
    if(!fc)
        return;

    auto fct = thread([&](){
        fc->start();
    });
    fct.detach();

    auto medias = fetch_sdp_media(r_string_utils::format("rtsp://127.0.0.1:%d/true_north_h265_aac.mkv", port));

    string video_codec_name, h265_codec_parameters;
    int video_timebase;
    tie(video_codec_name, h265_codec_parameters, video_timebase) = sdp_media_map_to_s(VIDEO_MEDIA, medias);

    auto h265_sps_b = get_h265_sps(h265_codec_parameters);

    auto sps_info = parse_h265_sps(h265_sps_b.value());

    RTF_ASSERT(sps_info.width == 96);
    RTF_ASSERT(sps_info.height = 56);
    RTF_ASSERT(sps_info.profile_idc == 1);
    RTF_ASSERT(sps_info.level_idc == 30);

    fc->quit();
}
#endif
