#include "test_r_playback_restream.h"
#include "r_vss/r_playback_restream.h"
#include "r_disco/r_devices.h"
#include "r_storage/r_storage_file.h"
#include "r_av/r_video_encoder.h"
#include "r_pipeline/r_gst_source.h"
#include "r_utils/r_file.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_time_utils.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_playback_restream);

static const char* const PB_TOP = "pb_top";
static const char* const PB_NTS = "./pb.nts";

void test_r_playback_restream::setup()
{
    r_pipeline::gstreamer_init();
    teardown();
}

void test_r_playback_restream::teardown()
{
    std::filesystem::remove_all(PB_TOP);
    std::filesystem::remove(PB_NTS);
}

namespace
{

// FRAMES frames at FPS, a key frame every second, recorded for camera "cam-pb" (named "porch") as revere stores a
// camera's H.264: byte-stream access units, the parameter sets in front of each key frame, and the caps the playback
// mount gives its appsrc. No camera, stream keeper or recording context exists: only the recording.
const int FPS = 10;
const int FRAMES = 40;

struct recording
{
    int64_t base {0};
    unique_ptr<r_disco::r_devices> devices;

    recording()
    {
        base = r_time_utils::tp_to_epoch_millis(std::chrono::system_clock::now()) - 400000;
        r_storage::r_storage_file::allocate(PB_NTS, 2 * 1024 * 1024, 4);
        {
            r_storage::r_storage_file sf(PB_NTS);
            const string caps = "video/x-h264, stream-format=(string)byte-stream, alignment=(string)au";
            r_nullable<string> vp;
            vp.set_value(string("encoded_video_sample_caps=") + r_string_utils::to_base64(caps.data(), caps.size()));
            auto wc = sf.create_write_context("h264", vp, r_storage::R_STORAGE_MEDIA_TYPE_VIDEO);

            const uint16_t W = 160, H = 90;
            r_av::r_video_encoder enc(AV_CODEC_ID_H264, 500000, W, H, {FPS, 1}, AV_PIX_FMT_YUV420P, 0, (uint16_t)FPS, AV_PROFILE_H264_MAIN, 30, "veryfast");
            const auto sps_pps = enc.get_extradata();
            int written = 0;
            auto drain = [&](r_av::r_codec_state st, const std::function<r_av::r_codec_state()>& again) {
                while(st == r_av::R_CODEC_STATE_HAS_OUTPUT)
                {
                    auto pi = enc.get();
                    vector<uint8_t> au;
                    if(pi.key)
                        au = sps_pps;
                    au.insert(au.end(), pi.data, pi.data + pi.size);
                    const int64_t ms = (int64_t)written * 1000 / FPS;
                    sf.write_frame(wc, r_storage::R_STORAGE_MEDIA_TYPE_VIDEO, au.data(), au.size(), pi.key, base + ms, ms);
                    ++written;
                    st = again();
                }
            };
            vector<uint8_t> yuv((size_t)W * H * 3 / 2);
            for(int i = 0; i < FRAMES; ++i)
            {
                memset(yuv.data(), 16 + i * 4, (size_t)W * H);
                memset(yuv.data() + (size_t)W * H, 128, (size_t)W * H / 2);
                enc.attach_buffer(yuv.data(), yuv.size(), i);
                drain(enc.encode(), [&]() { return enc.encode(); });
            }
            drain(enc.flush(), [&]() { return enc.flush(); });
            RTF_ASSERT(written == FRAMES);
        }

        r_fs::mkdir(PB_TOP);
        devices = make_unique<r_disco::r_devices>(PB_TOP);
        devices->start();
        r_disco::r_camera cam;
        cam.id = "cam-pb";
        cam.state = "assigned";
        cam.friendly_name.set_value("porch");
        cam.record_file_path.set_value(PB_NTS);
        devices->save_camera(cam);
    }

    ~recording() { devices->stop(); }

    // The playback path for from_ms to to_ms into the recording, as a client asks for it.
    string path(int64_t from_ms, int64_t to_ms) const
    {
        return "/porch_" +
            r_time_utils::tp_to_iso_8601(r_time_utils::epoch_millis_to_tp(base + from_ms), false) + "_" +
            r_time_utils::tp_to_iso_8601(r_time_utils::epoch_millis_to_tp(base + to_ms), false);
    }
};

// A GStreamer RTSP server on loopback, running its own main loop.
struct rtsp_server
{
    int port {0};
    GstRTSPServer* server {nullptr};
    GstRTSPMountPoints* mounts {nullptr};
    GMainContext* ctx {nullptr};
    GMainLoop* loop {nullptr};
    guint source_id {0};
    thread th;

    rtsp_server()
    {
        port = RTF_NEXT_PORT();
        server = gst_rtsp_server_new();
        gst_rtsp_server_set_address(server, "127.0.0.1");
        gst_rtsp_server_set_service(server, to_string(port).c_str());
        mounts = gst_rtsp_server_get_mount_points(server);
        ctx = g_main_context_new();
        loop = g_main_loop_new(ctx, FALSE);
        source_id = gst_rtsp_server_attach(server, ctx);
        RTF_ASSERT(source_id != 0);
        th = thread([this](){ g_main_loop_run(loop); });
    }

    ~rtsp_server()
    {
        g_main_loop_quit(loop);
        th.join();
        if(auto src = g_main_context_find_source_by_id(ctx, source_id))
            g_source_destroy(src);
        g_object_unref(mounts);
        g_main_loop_unref(loop);
        g_main_context_unref(ctx);
        g_object_unref(server);
    }

    string url(const string& path) const { return "rtsp://127.0.0.1:" + to_string(port) + path; }
};

// Plays url to its end (or until nothing comes for 5 s) and counts the video frames that arrive.
int frames_played(const string& url)
{
    auto launch = "rtspsrc location=" + url + " protocols=tcp latency=0 ! rtph264depay ! video/x-h264,stream-format=byte-stream,alignment=au ! appsink name=out sync=false";
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
    int frames = 0;
    while(auto sample = gst_app_sink_try_pull_sample(GST_APP_SINK(out), 5 * GST_SECOND))
    {
        ++frames;
        gst_sample_unref(sample);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(out);
    gst_object_unref(pipeline);
    return frames;
}

// A viewer that stays connected: take() waits for frames, and the session lasts until the viewer goes.
struct viewer
{
    GstElement* pipeline {nullptr};
    GstElement* out {nullptr};

    explicit viewer(const string& url)
    {
        auto launch = "rtspsrc location=" + url + " protocols=tcp latency=0 ! rtph264depay ! video/x-h264,stream-format=byte-stream,alignment=au ! appsink name=out sync=false";
        GError* err = nullptr;
        pipeline = gst_parse_launch(launch.c_str(), &err);
        if(err)
            g_error_free(err);
        RTF_ASSERT(pipeline != nullptr);
        out = gst_bin_get_by_name(GST_BIN(pipeline), "out");
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
    }

    ~viewer()
    {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(out);
        gst_object_unref(pipeline);
    }

    // Up to n frames, each within 5 s of the last; how many came.
    int take(int n)
    {
        int got = 0;
        while(got < n)
        {
            auto sample = gst_app_sink_try_pull_sample(GST_APP_SINK(out), 5 * GST_SECOND);
            if(!sample)
                break;
            gst_sample_unref(sample);
            ++got;
        }
        return got;
    }
};

// The status line of a DESCRIBE for url.
int describe_status(int port, const string& url)
{
    r_socket sok;
    sok.connect("127.0.0.1", port);
    string req = "DESCRIBE " + url + " RTSP/1.0\r\nCSeq: 1\r\nAccept: application/sdp\r\n\r\n";
    r_networking::r_send(sok, req.data(), req.size(), 5000);
    string head;
    char c;
    while(head.size() < 16384 && head.find("\r\n") == string::npos && r_networking::r_recv(sok, &c, 1, 10000) == 1)
        head.push_back(c);
    auto parts = r_string_utils::split(head, ' ');
    return parts.size() >= 2 ? r_string_utils::s_to_int(parts[1]) : -1;
}

}

void test_r_playback_restream::test_plays_with_no_camera_connected()
{
    recording rec;
    rtsp_server rs;

    // The whole recording, and past its end.
    auto path = rec.path(0, FRAMES * 1000 / FPS + 2000);
    RTF_ASSERT(add_playback_mount(rs.mounts, path, PB_TOP, *rec.devices, "cam-pb",
        rec.base, rec.base + FRAMES * 1000 / FPS + 2000));

    RTF_ASSERT(frames_played(rs.url(path)) == FRAMES);
}

void test_r_playback_restream::test_plays_to_the_end_of_its_range()
{
    recording rec;
    rtsp_server rs;

    // The first 2 s of 4: just the range is played, and the session's reader ends there, before the viewer leaves and
    // the session goes. That order crashed revere when the session lived on a recording context that went first: the
    // reader held the last reference, and the session's destructor ran on the reader's own thread (std::terminate).
    auto path = rec.path(0, 2000);
    RTF_ASSERT(add_playback_mount(rs.mounts, path, PB_TOP, *rec.devices, "cam-pb", rec.base, rec.base + 2000));

    auto n = frames_played(rs.url(path));
    RTF_ASSERT(n >= 2000 * FPS / 1000 && n <= 2000 * FPS / 1000 + 1);

    // Again: mounted again first, as the stream keeper does on the next OPTIONS for it (the mount went with the
    // session).
    RTF_ASSERT(add_playback_mount(rs.mounts, path, PB_TOP, *rec.devices, "cam-pb", rec.base, rec.base + 2000));
    auto again = frames_played(rs.url(path));
    RTF_ASSERT(again == n);
}

// Whether a DESCRIBE for url answers `status` within 3 s.
static bool describes_within(int port, const string& url, int status)
{
    for(int i = 0; i < 30; ++i)
    {
        if(describe_status(port, url) == status)
            return true;
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    return false;
}

void test_r_playback_restream::test_mount_goes_with_its_session()
{
    recording rec;
    rtsp_server rs;

    auto path = rec.path(0, 2000);
    RTF_ASSERT(add_playback_mount(rs.mounts, path, PB_TOP, *rec.devices, "cam-pb", rec.base, rec.base + 2000));
    RTF_ASSERT(frames_played(rs.url(path)) > 0);

    // Its session over, the mount is gone: mounts no longer pile up, one per play, for as long as revere runs.
    RTF_ASSERT(describes_within(rs.port, rs.url(path), 404));
}

void test_r_playback_restream::test_newer_mount_stays()
{
    recording rec;
    rtsp_server rs;

    auto path = rec.path(0, 4000);
    RTF_ASSERT(add_playback_mount(rs.mounts, path, PB_TOP, *rec.devices, "cam-pb", rec.base, rec.base + 4000));
    {
        viewer first(rs.url(path));
        RTF_ASSERT(first.take(5) == 5);
        // Another viewer's request for the same URL mounts it again, in place of the first's.
        RTF_ASSERT(add_playback_mount(rs.mounts, path, PB_TOP, *rec.devices, "cam-pb", rec.base, rec.base + 4000));
    }
    // The first session ends without taking the newer mount with it.
    this_thread::sleep_for(chrono::seconds(1));
    RTF_ASSERT(describe_status(rs.port, rs.url(path)) == 200);
}

void test_r_playback_restream::test_nothing_recorded_mounts_nothing()
{
    recording rec;
    rtsp_server rs;

    auto path = rec.path(-60000, -30000);
    RTF_ASSERT(!add_playback_mount(rs.mounts, path, PB_TOP, *rec.devices, "cam-pb", rec.base - 60000, rec.base - 30000));
    RTF_ASSERT(describe_status(rs.port, rs.url(path)) == 404);
}

void test_r_playback_restream::test_playback_camera()
{
    vector<r_disco::r_camera> cameras(3);
    cameras[0].id = "unnamed";
    cameras[1].id = "front";
    cameras[1].friendly_name.set_value("front door");
    cameras[2].id = "back";
    cameras[2].friendly_name.set_value("back");

    // Matched by its name as the playback URL writes it (friendly_mount_path, less its "/").
    auto front_name = friendly_mount_path("front door").substr(1);
    auto front = playback_camera(cameras, front_name);
    RTF_ASSERT(!front.is_null() && front.value().id == "front");
    auto back = playback_camera(cameras, "back");
    RTF_ASSERT(!back.is_null() && back.value().id == "back");
    RTF_ASSERT(playback_camera(cameras, "side").is_null());
    RTF_ASSERT(playback_camera(cameras, "").is_null());
    RTF_ASSERT(playback_camera(vector<r_disco::r_camera>(), "back").is_null());
}

// Whether something is mounted at exactly path.
static bool mounted(GstRTSPMountPoints* mounts, const string& path)
{
    gint matched = 0;
    auto f = gst_rtsp_mount_points_match(mounts, path.c_str(), &matched);
    if(!f)
        return false;
    g_object_unref(f);
    return matched == (gint)path.size();
}

void test_r_playback_restream::test_unplayed_mount_frees_its_mount_points()
{
    // A mount never played and never removed: its mount points still go when their last owner lets go (the mount
    // used to hold a ref on them, a cycle through the factory that kept them past the server).
    recording rec;
    auto mounts = gst_rtsp_mount_points_new();
    gpointer alive = mounts;
    g_object_add_weak_pointer(G_OBJECT(mounts), &alive);
    auto path = rec.path(0, 2000);
    RTF_ASSERT(add_playback_mount(mounts, path, PB_TOP, *rec.devices, "cam-pb", rec.base, rec.base + 2000));
    RTF_ASSERT(mounted(mounts, path));
    g_object_unref(mounts);
    RTF_ASSERT(alive == nullptr);
}

void test_r_playback_restream::test_unused_mount_expires()
{
    recording rec;
    rtsp_server rs;
    const int64_t t = playback_mount_clock_ms();

    // Mounted by an OPTIONS whose client never got to a media: it goes UNUSED_PLAYBACK_MOUNT_MS later, not before.
    auto quiet = rec.path(0, 1000);
    RTF_ASSERT(add_playback_mount(rs.mounts, quiet, PB_TOP, *rec.devices, "cam-pb", rec.base, rec.base + 1000, t));
    RTF_ASSERT_EQUAL(remove_unused_playback_mounts(rs.mounts, t + UNUSED_PLAYBACK_MOUNT_MS - 1), (size_t)0);
    RTF_ASSERT(mounted(rs.mounts, quiet));
    RTF_ASSERT_EQUAL(remove_unused_playback_mounts(rs.mounts, t + UNUSED_PLAYBACK_MOUNT_MS), (size_t)1);
    RTF_ASSERT(!mounted(rs.mounts, quiet));
    RTF_ASSERT_EQUAL(remove_unused_playback_mounts(rs.mounts, t + 10 * UNUSED_PLAYBACK_MOUNT_MS), (size_t)0);

    // Mounted again later for the same URL: the newer mount's own time counts.
    RTF_ASSERT(add_playback_mount(rs.mounts, quiet, PB_TOP, *rec.devices, "cam-pb", rec.base, rec.base + 1000, t));
    RTF_ASSERT(add_playback_mount(rs.mounts, quiet, PB_TOP, *rec.devices, "cam-pb", rec.base, rec.base + 1000, t + 50000));
    RTF_ASSERT_EQUAL(remove_unused_playback_mounts(rs.mounts, t + UNUSED_PLAYBACK_MOUNT_MS), (size_t)0);
    RTF_ASSERT(mounted(rs.mounts, quiet));
    // And the next mount request clears it once it's due, as it mounts its own.
    auto other = rec.path(0, 2000);
    RTF_ASSERT(add_playback_mount(rs.mounts, other, PB_TOP, *rec.devices, "cam-pb", rec.base, rec.base + 2000, t + 50000 + UNUSED_PLAYBACK_MOUNT_MS));
    RTF_ASSERT(!mounted(rs.mounts, quiet));
    RTF_ASSERT(mounted(rs.mounts, other));

    // One a session is playing from is never taken by it; the session's end removes it.
    {
        viewer v(rs.url(other));
        RTF_ASSERT(v.take(3) > 0);
        RTF_ASSERT_EQUAL(remove_unused_playback_mounts(rs.mounts, t + 100 * UNUSED_PLAYBACK_MOUNT_MS), (size_t)0);
        RTF_ASSERT(mounted(rs.mounts, other));
        RTF_ASSERT(v.take(3) > 0);
    }
    RTF_ASSERT(describes_within(rs.port, rs.url(other), 404));

    // Something else mounted at the URL since (not a playback mount): left where it is.
    auto third = rec.path(500, 1500);
    RTF_ASSERT(add_playback_mount(rs.mounts, third, PB_TOP, *rec.devices, "cam-pb", rec.base + 500, rec.base + 1500, t));
    auto foreign = gst_rtsp_media_factory_new();
    gst_rtsp_mount_points_add_factory(rs.mounts, third.c_str(), foreign);
    RTF_ASSERT_EQUAL(remove_unused_playback_mounts(rs.mounts, t + 200 * UNUSED_PLAYBACK_MOUNT_MS), (size_t)0);
    RTF_ASSERT(mounted(rs.mounts, third));
    gst_rtsp_mount_points_remove_factory(rs.mounts, third.c_str());
}
