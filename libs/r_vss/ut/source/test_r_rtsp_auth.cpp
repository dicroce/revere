#include "test_r_rtsp_auth.h"
#include "r_vss/r_rtsp_auth.h"
#include "r_vss/r_live_restream_host.h"
#include "rtsp_test_client.h"
#include "r_pipeline/r_gst_source.h"
#include "r_utils/r_file.h"
#include "r_utils/r_md5.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <cstring>
#include <string>
#include <thread>

#ifndef IS_WINDOWS
#include <sys/stat.h>
#endif

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace rtsp_test_client;

REGISTER_TEST_FIXTURE(test_r_rtsp_auth);

void test_r_rtsp_auth::test_password_persisted()
{
    auto dir = r_fs::temp_file_name(r_fs::working_directory(), "rtsp_auth_ut_");
    r_fs::mkdir_p(dir);

    auto a = r_rtsp_auth::load_or_create_password(dir);
    RTF_ASSERT(a.size() == 64);
    // Stable across restarts (VLC users keep a working URL).
    RTF_ASSERT(r_rtsp_auth::load_or_create_password(dir) == a);

#ifndef IS_WINDOWS
    struct stat st;
    RTF_ASSERT(::stat((dir + "/rtsp_password").c_str(), &st) == 0);
    RTF_ASSERT((st.st_mode & 0777) == 0600);
#endif

    // A truncated file is replaced rather than trusted.
    r_fs::atomic_write_private_file((const uint8_t*)"short", 5, dir + PATH_SLASH + "rtsp_password");
    auto b = r_rtsp_auth::load_or_create_password(dir);
    RTF_ASSERT(b.size() == 64 && b != a);

    r_fs::remove_file(dir + PATH_SLASH + "rtsp_password");
    r_fs::rmdir(dir);
}

void test_r_rtsp_auth::test_digest_required()
{
    r_pipeline::gstreamer_init();

    const string pw = "correct-horse-battery-staple-0123456789abcdef";
    int port = RTF_NEXT_PORT();

    auto server = gst_rtsp_server_new();
    gst_rtsp_server_set_address(server, "127.0.0.1");
    gst_rtsp_server_set_service(server, to_string(port).c_str());
    auto auth = r_rtsp_auth::make_auth(pw);
    gst_rtsp_server_set_auth(server, auth);

    auto mounts = gst_rtsp_server_get_mount_points(server);
    // A viewable stream, and one nobody was granted.
    auto viewable = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(viewable, "( videotestsrc is-live=true ! fakesink name=pay0 )");
    r_rtsp_auth::allow_viewer(viewable);
    gst_rtsp_mount_points_add_factory(mounts, "/cam", viewable);
    auto locked = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(locked, "( videotestsrc is-live=true ! fakesink name=pay0 )");
    gst_rtsp_mount_points_add_factory(mounts, "/locked", locked);
    g_object_unref(mounts);

    auto ctx = g_main_context_new();
    auto loop = g_main_loop_new(ctx, FALSE);
    auto source_id = gst_rtsp_server_attach(server, ctx);
    RTF_ASSERT(source_id != 0);
    thread th([&](){ g_main_loop_run(loop); });

    const string base = "rtsp://127.0.0.1:" + to_string(port);
    const string url = base + "/cam";

    {
        r_socket sok;
        sok.connect("127.0.0.1", port);

        // OPTIONS is answered without credentials (gst-rtsp-server checks
        // them per stream).
        RTF_ASSERT(_request(sok, "OPTIONS", url, 1).status == 200);

        // No credentials: refused, with a Digest (not Basic) challenge.
        auto r = _request(sok, "DESCRIBE", url, 2);
        RTF_ASSERT(r.status == 401);
        RTF_ASSERT(r_string_utils::starts_with(r.www_authenticate, "Digest"));
        auto realm = _param(r.www_authenticate, "realm");
        auto nonce = _param(r.www_authenticate, "nonce");
        RTF_ASSERT(!nonce.empty());

        // Wrong password: refused.
        RTF_ASSERT(_request(sok, "DESCRIBE", url, 3, _digest(r_rtsp_auth::RTSP_USER, "wrong", realm, nonce, "DESCRIBE", url)).status == 401);
        // Right password, wrong user: refused.
        RTF_ASSERT(_request(sok, "DESCRIBE", url, 4, _digest("admin", pw, realm, nonce, "DESCRIBE", url)).status == 401);
        // Basic with the right password: refused (Digest only, so the
        // password never crosses the network).
        auto basic = string("Authorization: Basic ") + r_string_utils::to_base64((const uint8_t*)(string(r_rtsp_auth::RTSP_USER) + ":" + pw).c_str(), strlen(r_rtsp_auth::RTSP_USER) + 1 + pw.size()) + "\r\n";
        RTF_ASSERT(_request(sok, "DESCRIBE", url, 5, basic).status == 401);

        // Right credential: past authentication (200 with an SDP, or a
        // pipeline error from the test launch line; anything but 401).
        auto ok = _request(sok, "DESCRIBE", url, 6, _digest(r_rtsp_auth::RTSP_USER, pw, realm, nonce, "DESCRIBE", url));
        RTF_ASSERT(ok.status != 401 && ok.status != 0);

        // A stream without the viewer role stays closed even then.
        auto lurl = base + "/locked";
        auto lr = _request(sok, "DESCRIBE", lurl, 7, _digest(r_rtsp_auth::RTSP_USER, pw, realm, nonce, "DESCRIBE", lurl));
        RTF_ASSERT(lr.status == 401 || lr.status == 404);
    }

    g_main_loop_quit(loop);
    th.join();
    // Detach the server from ctx (g_source_remove() only looks in the default context).
    if(auto src = g_main_context_find_source_by_id(ctx, source_id))
        g_source_destroy(src);
    g_main_loop_unref(loop);
    g_main_context_unref(ctx);
    g_object_unref(server);
    g_object_unref(auth);
}

// A camera is also mounted by its id, at the same stream and behind the same credential; a rename moves the
// friendly-named mount and leaves this one; removing the camera removes both.
void test_r_rtsp_auth::test_by_id_mount()
{
    r_pipeline::gstreamer_init();

    const string pw = "correct-horse-battery-staple-0123456789abcdef";
    const string id = "0b3f1c9e-5a7d-3c1e-9f2a-6d8e4b7a1c20";
    int port = RTF_NEXT_PORT();

    auto server = gst_rtsp_server_new();
    gst_rtsp_server_set_address(server, "127.0.0.1");
    gst_rtsp_server_set_service(server, to_string(port).c_str());
    auto auth = r_rtsp_auth::make_auth(pw);
    gst_rtsp_server_set_auth(server, auth);

    auto mounts = gst_rtsp_server_get_mount_points(server);
    auto factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, "( videotestsrc is-live=true ! fakesink name=pay0 )");
    r_rtsp_auth::allow_viewer(factory);

    const string friendly = "/Front_Door";
    const string by_id = restream_id_mount_path(id);
    RTF_ASSERT(by_id == "/camera/" + id);
    add_factory_at_both(mounts, factory, friendly, by_id);

    auto serves = [&](const string& path) {
        gint matched = 0;
        auto f = gst_rtsp_mount_points_match(mounts, path.c_str(), &matched);
        bool exact = f && matched == (gint)path.size();
        if(f)
            g_object_unref(f);
        return exact;
    };

    // Both paths are the one factory: the same stream, the same viewer role.
    {
        gint m1 = 0, m2 = 0;
        auto f1 = gst_rtsp_mount_points_match(mounts, friendly.c_str(), &m1);
        auto f2 = gst_rtsp_mount_points_match(mounts, by_id.c_str(), &m2);
        RTF_ASSERT(f1 && f1 == f2);
        g_object_unref(f1);
        g_object_unref(f2);
    }

    auto ctx = g_main_context_new();
    auto loop = g_main_loop_new(ctx, FALSE);
    auto source_id = gst_rtsp_server_attach(server, ctx);
    RTF_ASSERT(source_id != 0);
    thread th([&](){ g_main_loop_run(loop); });

    const string base = "rtsp://127.0.0.1:" + to_string(port);
    const string url = base + by_id;

    {
        r_socket sok;
        sok.connect("127.0.0.1", port);

        // The credential is required here as on the friendly mount: none, wrong, and Basic are all refused.
        auto r = _request(sok, "DESCRIBE", url, 1);
        RTF_ASSERT(r.status == 401);
        RTF_ASSERT(r_string_utils::starts_with(r.www_authenticate, "Digest"));
        auto realm = _param(r.www_authenticate, "realm");
        auto nonce = _param(r.www_authenticate, "nonce");
        RTF_ASSERT(_request(sok, "DESCRIBE", url, 2, _digest(r_rtsp_auth::RTSP_USER, "wrong", realm, nonce, "DESCRIBE", url)).status == 401);
        auto basic = string("Authorization: Basic ") + r_string_utils::to_base64((const uint8_t*)(string(r_rtsp_auth::RTSP_USER) + ":" + pw).c_str(), strlen(r_rtsp_auth::RTSP_USER) + 1 + pw.size()) + "\r\n";
        RTF_ASSERT(_request(sok, "DESCRIBE", url, 3, basic).status == 401);
        auto ok = _request(sok, "DESCRIBE", url, 4, _digest(r_rtsp_auth::RTSP_USER, pw, realm, nonce, "DESCRIBE", url));
        RTF_ASSERT(ok.status != 401 && ok.status != 404 && ok.status != 0);

        // A rename takes the friendly-named mount away and puts another at the new name; the id's mount is untouched.
        gst_rtsp_mount_points_remove_factory(mounts, friendly.c_str());
        RTF_ASSERT(!serves(friendly));
        RTF_ASSERT(serves(by_id));
        auto still = _request(sok, "DESCRIBE", url, 5, _digest(r_rtsp_auth::RTSP_USER, pw, realm, nonce, "DESCRIBE", url));
        RTF_ASSERT(still.status != 401 && still.status != 404 && still.status != 0);
    }

    // Removing the camera removes the id's mount too.
    gst_rtsp_mount_points_remove_factory(mounts, by_id.c_str());
    RTF_ASSERT(!serves(by_id));
    RTF_ASSERT(!serves(friendly));
    g_object_unref(mounts);

    g_main_loop_quit(loop);
    th.join();
    if(auto src = g_main_context_find_source_by_id(ctx, source_id))
        g_source_destroy(src);
    g_main_loop_unref(loop);
    g_main_context_unref(ctx);
    g_object_unref(server);
    g_object_unref(auth);
}
