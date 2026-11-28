#include "test_r_restream_mount.h"
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include "r_pipeline/r_gst_source.h"
#include "r_vss/r_live_restream_host.h"
#include <string>

using namespace std;
using namespace r_vss;

REGISTER_TEST_FIXTURE(test_r_restream_mount);

void test_r_restream_mount::setup()
{
    r_pipeline::gstreamer_init();
}

void test_r_restream_mount::test_url()
{
    RTF_ASSERT(restream_url("127.0.0.1", 8554, "Street View", "revere", "s3cret") == "rtsp://revere:s3cret@127.0.0.1:8554/Street_View");
    RTF_ASSERT(restream_url("192.168.1.20", 8554, "Front") == "rtsp://192.168.1.20:8554/Front");
    // The reserved "camera/" prefix is kept clear, as the mount is (friendly_mount_path).
    RTF_ASSERT(restream_url("127.0.0.1", 8554, "camera") == "rtsp://127.0.0.1:8554/camera_");
}

// The path gst-rtsp-server takes from a request for url, as it does on DESCRIBE.
static string _requested_path(GstRTSPMountPoints* mounts, const string& url)
{
    GstRTSPUrl* u = nullptr;
    if(gst_rtsp_url_parse(url.c_str(), &u) != GST_RTSP_OK || !u)
        return string();
    auto p = gst_rtsp_mount_points_make_path(mounts, u);
    gst_rtsp_url_free(u);
    string path = p ? p : "";
    g_free(p);
    return path;
}

// Whether a request for url finds the factory mounted for the camera, by the server's own lookup (an exact match:
// a shorter mount matching a prefix doesn't count).
static bool _reaches(GstRTSPMountPoints* mounts, GstRTSPMediaFactory* factory, const string& url)
{
    auto path = _requested_path(mounts, url);
    if(path.empty())
        return false;
    gint matched = 0;
    auto found = gst_rtsp_mount_points_match(mounts, path.c_str(), &matched);
    bool same = found == factory && matched == (gint)path.size();
    if(found)
        g_object_unref(found);
    return same;
}

void test_r_restream_mount::test_url_reaches_the_mount()
{
    // The camera's mount at its friendly name's path, as the stream keeper adds it, and the URL the UI copies.
    const string name = "Street View";
    auto mounts = gst_rtsp_mount_points_new();
    auto factory = gst_rtsp_media_factory_new();
    g_object_ref(factory);   // add_factory takes one reference; the test keeps its own
    gst_rtsp_mount_points_add_factory(mounts, friendly_mount_path(name).c_str(), factory);

    RTF_ASSERT(_reaches(mounts, factory, restream_url("127.0.0.1", 8554, name, "revere", "s3cret")));

    // The URL as it was built before, from the raw name: no such mount, so a 404.
    RTF_ASSERT(!_reaches(mounts, factory, "rtsp://revere:s3cret@127.0.0.1:8554/Street View"));
    RTF_ASSERT(!_reaches(mounts, factory, "rtsp://revere:s3cret@127.0.0.1:8554/Street%20View"));

    g_object_unref(factory);
    g_object_unref(mounts);
}
