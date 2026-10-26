#include "test_r_rtsp_auth.h"
#include "r_vss/r_rtsp_auth.h"
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

namespace
{

string _md5(const string& s)
{
    r_md5 h;
    h.update((const uint8_t*)s.data(), s.size());
    h.finalize();
    return h.get_as_string();
}

struct rtsp_reply
{
    int status {0};
    string www_authenticate;
};

// One request/response on an open RTSP connection (headers only; a body, if
// any, is drained by Content-Length).
rtsp_reply _request(r_socket& sok, const string& method, const string& url, int cseq, const string& extra_headers = string())
{
    string req = method + " " + url + " RTSP/1.0\r\nCSeq: " + to_string(cseq) + "\r\n" + extra_headers + "\r\n";
    r_networking::r_send(sok, req.data(), req.size(), 5000);

    string head;
    char c;
    while(head.size() < 16384 && head.find("\r\n\r\n") == string::npos)
    {
        if(r_networking::r_recv(sok, &c, 1, 5000) != 1)
            break;
        head.push_back(c);
    }

    rtsp_reply r;
    auto lines = r_string_utils::split(head, "\r\n");
    if(!lines.empty())
    {
        auto parts = r_string_utils::split(lines[0], ' ');
        if(parts.size() >= 2)
            r.status = r_string_utils::s_to_int(parts[1]);
    }
    size_t content_length = 0;
    for(auto& l : lines)
    {
        auto lower = r_string_utils::to_lower(l);
        if(lower.compare(0, 17, "www-authenticate:") == 0)
            r.www_authenticate = r_string_utils::strip(l.substr(17));
        if(lower.compare(0, 15, "content-length:") == 0)
            content_length = (size_t)r_string_utils::s_to_int(r_string_utils::strip(l.substr(15)));
    }
    if(content_length > 0)
    {
        string body(content_length, '\0');
        r_networking::r_recv(sok, &body[0], content_length, 5000);
    }
    return r;
}

// value of key="..." in a challenge.
string _param(const string& challenge, const string& key)
{
    auto k = key + "=\"";
    auto p = challenge.find(k);
    if(p == string::npos)
        return string();
    p += k.size();
    auto e = challenge.find('"', p);
    return e == string::npos ? string() : challenge.substr(p, e - p);
}

string _digest(const string& user, const string& pw, const string& realm, const string& nonce, const string& method, const string& uri)
{
    auto ha1 = _md5(user + ":" + realm + ":" + pw);
    auto ha2 = _md5(method + ":" + uri);
    auto response = _md5(ha1 + ":" + nonce + ":" + ha2);
    return "Authorization: Digest username=\"" + user + "\", realm=\"" + realm + "\", nonce=\"" + nonce +
           "\", uri=\"" + uri + "\", response=\"" + response + "\"\r\n";
}

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
