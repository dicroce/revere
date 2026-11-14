#include "test_r_rtsp_logins.h"
#include "r_vss/r_rtsp_auth.h"
#include "r_vss/r_rtsp_logins.h"
#include "r_vss/r_rtsp_logins_api.h"
#include "rtsp_test_client.h"
#include "r_pipeline/r_gst_source.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_http/r_web_server.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/3rdparty/json/json.h"

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace r_vss;
using namespace r_utils;
using namespace r_http;
using namespace rtsp_test_client;
using namespace std;
using namespace std::chrono;
using json = nlohmann::json;

REGISTER_TEST_FIXTURE(test_r_rtsp_logins);

namespace
{

const string SHARED_PW = "correct-horse-battery-staple-0123456789abcdef";

// A real RTSP server on loopback with the shared login, one camera's stream mounted at its friendly name and at an
// id-style path (one factory at both, as a camera is), and the per-client logins on it.
struct rtsp_fixture
{
    GstRTSPServer* server {nullptr};
    GstRTSPAuth* auth {nullptr};
    GMainContext* ctx {nullptr};
    GMainLoop* loop {nullptr};
    guint source_id {0};
    thread th;
    int port {0};
    unique_ptr<r_rtsp_logins> logins;

    static constexpr const char* FRIENDLY = "/Front_Door";
    static constexpr const char* BY_ID = "/camera/0b3f1c9e-5a7d-3c1e-9f2a-6d8e4b7a1c20";

    rtsp_fixture(int at_port, const char* launch)
    {
        r_pipeline::gstreamer_init();
        port = at_port;
        server = gst_rtsp_server_new();
        gst_rtsp_server_set_address(server, "127.0.0.1");
        gst_rtsp_server_set_service(server, to_string(port).c_str());
        auth = r_rtsp_auth::make_auth(SHARED_PW);
        gst_rtsp_server_set_auth(server, auth);
        logins = make_unique<r_rtsp_logins>(server, auth);

        auto mounts = gst_rtsp_server_get_mount_points(server);
        auto factory = gst_rtsp_media_factory_new();
        gst_rtsp_media_factory_set_launch(factory, launch);
        gst_rtsp_media_factory_set_shared(factory, FALSE);
        r_rtsp_auth::allow_viewer(factory);
        g_object_ref(factory);
        gst_rtsp_mount_points_add_factory(mounts, FRIENDLY, factory);
        gst_rtsp_mount_points_add_factory(mounts, BY_ID, factory);
        g_object_unref(mounts);

        ctx = g_main_context_new();
        loop = g_main_loop_new(ctx, FALSE);
        source_id = gst_rtsp_server_attach(server, ctx);
        th = thread([this](){ g_main_loop_run(loop); });
    }

    ~rtsp_fixture()
    {
        logins.reset();
        g_main_loop_quit(loop);
        th.join();
        if(auto src = g_main_context_find_source_by_id(ctx, source_id))
            g_source_destroy(src);
        g_main_loop_unref(loop);
        g_main_context_unref(ctx);
        g_object_unref(server);
        g_object_unref(auth);
    }

    string url(const string& path) const { return "rtsp://127.0.0.1:" + to_string(port) + path; }

    // The status of a DESCRIBE of `path` as `user`: 401 when refused, otherwise past authentication.
    int describe(const string& path, const string& user, const string& pw)
    {
        r_socket sok;
        sok.connect("127.0.0.1", port);
        auto u = url(path);
        auto challenge = _request(sok, "DESCRIBE", u, 1);
        RTF_ASSERT(challenge.status == 401);
        auto realm = _param(challenge.www_authenticate, "realm");
        auto nonce = _param(challenge.www_authenticate, "nonce");
        return _request(sok, "DESCRIBE", u, 2, _digest(user, pw, realm, nonce, "DESCRIBE", u)).status;
    }
};

bool _past_auth(int status) { return status != 401 && status != 403 && status != 404 && status != 0; }

}

// A login made at run time is accepted on both mounts (by friendly name and by id) and refused once removed; the
// shared login is the same throughout.
void test_r_rtsp_logins::test_login_reaches_mounts_until_removed()
{
    rtsp_fixture fx(RTF_NEXT_PORT(), "( videotestsrc is-live=true ! fakesink name=pay0 )");

    RTF_ASSERT(fx.logins->list().empty());
    auto a = fx.logins->add("Living room tablet");
    auto b = fx.logins->add("Kitchen hub");
    RTF_ASSERT(!a.is_null() && !b.is_null());
    RTF_ASSERT(a.value().username != b.value().username);
    RTF_ASSERT(a.value().username.compare(0, 2, "c-") == 0 && a.value().username.size() == 14);
    RTF_ASSERT(a.value().password.size() >= 32);

    for(auto path : {rtsp_fixture::FRIENDLY, rtsp_fixture::BY_ID})
    {
        RTF_ASSERT(_past_auth(fx.describe(path, a.value().username, a.value().password)));
        RTF_ASSERT(_past_auth(fx.describe(path, b.value().username, b.value().password)));
        RTF_ASSERT(_past_auth(fx.describe(path, r_rtsp_auth::RTSP_USER, SHARED_PW)));
        // A login's password is its own.
        RTF_ASSERT(fx.describe(path, a.value().username, b.value().password) == 401);
    }

    RTF_ASSERT(fx.logins->remove(a.value().username));
    RTF_ASSERT(!fx.logins->remove(a.value().username));
    RTF_ASSERT(!fx.logins->remove("c-000000000000"));
    RTF_ASSERT(!fx.logins->remove(r_rtsp_auth::RTSP_USER));       // the shared login is not one of these

    for(auto path : {rtsp_fixture::FRIENDLY, rtsp_fixture::BY_ID})
    {
        RTF_ASSERT(fx.describe(path, a.value().username, a.value().password) == 401);
        RTF_ASSERT(_past_auth(fx.describe(path, b.value().username, b.value().password)));
        RTF_ASSERT(_past_auth(fx.describe(path, r_rtsp_auth::RTSP_USER, SHARED_PW)));
    }
    RTF_ASSERT_EQUAL(fx.logins->list().size(), (size_t)1);
}

namespace
{

// Plays `url` as user/pw over `protocols` and counts the buffers that arrive.
struct player
{
    GstElement* pipeline {nullptr};
    atomic<int> buffers {0};

    static void _handoff(GstElement*, GstBuffer*, GstPad*, gpointer d) { ++((player*)d)->buffers; }

    player(const string& url, const string& user, const string& pw, const char* protocols)
    {
        auto desc = "rtspsrc location=" + url + " user-id=" + user + " user-pw=" + pw + " protocols=" + protocols +
                    " latency=0 ! fakesink name=sink signal-handoffs=true sync=false";
        GError* err = nullptr;
        pipeline = gst_parse_launch(desc.c_str(), &err);
        if(err)
            g_error_free(err);
        RTF_ASSERT(pipeline != nullptr);
        auto sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
        g_signal_connect(sink, "handoff", G_CALLBACK(_handoff), this);
        gst_object_unref(sink);
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
    }

    ~player()
    {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }

    bool wait_for(int n, int seconds)
    {
        auto deadline = steady_clock::now() + chrono::seconds(seconds);
        while(buffers < n && steady_clock::now() < deadline)
            this_thread::sleep_for(milliseconds(50));
        return buffers >= n;
    }
};

}

// Removing a login stops video already playing under it, over both transports; another login's stream and the shared
// login's keep going. (Taking the login out of the server's table alone leaves a playing stream running.)
void test_r_rtsp_logins::test_removal_ends_a_playing_stream()
{
    r_pipeline::gstreamer_init();
    if(!gst_element_factory_find("x264enc") || !gst_element_factory_find("rtph264pay"))
        return;        // nothing to serve here

    for(auto protocols : {"tcp", "udp"})
    {
        rtsp_fixture fx(RTF_NEXT_PORT(), "( videotestsrc is-live=true ! video/x-raw,width=160,height=120,framerate=10/1 ! x264enc tune=zerolatency key-int-max=10 ! rtph264pay name=pay0 pt=96 config-interval=1 )");
        auto gone = fx.logins->add("to remove");
        auto kept = fx.logins->add("to keep");
        RTF_ASSERT(!gone.is_null() && !kept.is_null());

        // By friendly name and by id, so both mounts are covered.
        player p_gone(fx.url(rtsp_fixture::BY_ID), gone.value().username, gone.value().password, protocols);
        player p_kept(fx.url(rtsp_fixture::FRIENDLY), kept.value().username, kept.value().password, protocols);
        player p_shared(fx.url(rtsp_fixture::FRIENDLY), r_rtsp_auth::RTSP_USER, SHARED_PW, protocols);
        RTF_ASSERT(p_gone.wait_for(10, 20));
        RTF_ASSERT(p_kept.wait_for(10, 20));
        RTF_ASSERT(p_shared.wait_for(10, 20));

        RTF_ASSERT(fx.logins->remove(gone.value().username));

        // What was in flight lands, then nothing more.
        this_thread::sleep_for(milliseconds(1500));
        auto at = p_gone.buffers.load();
        auto kept_at = p_kept.buffers.load();
        auto shared_at = p_shared.buffers.load();
        this_thread::sleep_for(milliseconds(2000));
        RTF_ASSERT_EQUAL(p_gone.buffers.load(), at);
        RTF_ASSERT(p_kept.buffers.load() > kept_at + 5);
        RTF_ASSERT(p_shared.buffers.load() > shared_at + 5);
    }
}

namespace
{

const string LOCAL_TOKEN = "local-token-0123456789abcdef0123456789abcdef0123456789abcdef012345";
const string SESSION_TOKEN = "session-token-0123456789abcdef0123456789abcdef0123456789abcdef01";

rtsp_logins_api _api()
{
    rtsp_logins_api api;
    api.request_token = [](const r_server_request& request) {
        auto hn = request.get_header("authorization");
        string h = hn.is_null() ? string() : hn.value();
        return h.compare(0, 7, "Bearer ") == 0 ? h.substr(7) : string();
    };
    api.is_local_token = [](const string& t) { return t == LOCAL_TOKEN; };
    return api;
}

// The routes on a real web server (loopback), over an RTSP server with no mounts.
struct api_fixture
{
    rtsp_fixture rtsp;
    int port;
    r_web_server<r_socket> ws;

    api_fixture() : rtsp(RTF_NEXT_PORT(), "( videotestsrc is-live=true ! fakesink name=pay0 )"), port(RTF_NEXT_PORT()), ws(port, "127.0.0.1")
    {
        add_rtsp_login_routes(ws, *rtsp.logins, _api());
        ws.start();
        for(int i = 0; i < 200; ++i)
        {
            try
            {
                r_socket probe;
                probe.connect("127.0.0.1", port);
                probe.close();
                return;
            }
            catch(...)
            {
                this_thread::sleep_for(milliseconds(10));
            }
        }
    }
    ~api_fixture() { ws.stop(); }

    struct reply
    {
        int status {0};
        string body;
    };

    reply call(int method, const string& path, const string& token, const string& body = string())
    {
        r_client_request request("127.0.0.1", port);
        request.set_method(method);
        request.set_uri(path);
        if(!token.empty())
            request.add_header("Authorization", "Bearer " + token);
        if(!body.empty())
        {
            request.set_content_type("application/json");
            request.set_body(body);
        }
        r_socket sok;
        sok.connect("127.0.0.1", port);
        request.write_request(sok);
        r_client_response response;
        response.read_response(sok);
        reply r;
        r.status = response.get_status();
        auto b = response.get_body_as_string();
        if(!b.is_null())
            r.body = b.value();
        return r;
    }
};

}

// Every route: the local token works; a session token or none does not, and nothing changes.
void test_r_rtsp_logins::test_routes_need_the_local_token()
{
    api_fixture fx;
    auto made = fx.call(METHOD_POST, "/rtsp_logins", LOCAL_TOKEN, R"({"name":"hub"})");
    RTF_ASSERT_EQUAL(made.status, 200);
    auto username = json::parse(made.body)["username"].get<string>();
    RTF_ASSERT(json::parse(made.body)["password"].get<string>().size() >= 32);

    // A session token: refused on each route, whether or not it is a valid one.
    RTF_ASSERT_EQUAL(fx.call(METHOD_POST, "/rtsp_logins", SESSION_TOKEN, R"({"name":"x"})").status, 403);
    RTF_ASSERT_EQUAL(fx.call(METHOD_GET, "/rtsp_logins", SESSION_TOKEN).status, 403);
    RTF_ASSERT_EQUAL(fx.call(METHOD_DELETE, "/rtsp_logins/" + username, SESSION_TOKEN).status, 403);
    // No token at all.
    RTF_ASSERT_EQUAL(fx.call(METHOD_POST, "/rtsp_logins", "", R"({"name":"x"})").status, 401);
    RTF_ASSERT_EQUAL(fx.call(METHOD_GET, "/rtsp_logins", "").status, 401);
    RTF_ASSERT_EQUAL(fx.call(METHOD_DELETE, "/rtsp_logins/" + username, "").status, 401);
    // None of that touched anything.
    RTF_ASSERT_EQUAL(fx.rtsp.logins->list().size(), (size_t)1);

    RTF_ASSERT_EQUAL(fx.call(METHOD_DELETE, "/rtsp_logins/" + username, LOCAL_TOKEN).status, 204);
    RTF_ASSERT_EQUAL(fx.call(METHOD_DELETE, "/rtsp_logins/" + username, LOCAL_TOKEN).status, 404);
    RTF_ASSERT_EQUAL(fx.call(METHOD_DELETE, "/rtsp_logins", LOCAL_TOKEN).status, 404);
    RTF_ASSERT_EQUAL(fx.call(METHOD_POST, "/rtsp_logins", LOCAL_TOKEN, "{}").status, 400);
    RTF_ASSERT_EQUAL(fx.call(METHOD_POST, "/rtsp_logins", LOCAL_TOKEN, R"({"name":""})").status, 400);
    RTF_ASSERT_EQUAL(fx.call(METHOD_POST, "/rtsp_logins", LOCAL_TOKEN, "nonsense").status, 400);
    RTF_ASSERT_EQUAL(fx.call(METHOD_GET, "/rtsp_loginsX", LOCAL_TOKEN).status, 404);
    RTF_ASSERT(fx.rtsp.logins->list().empty());
}

// The loopback check on its own: a caller that isn't on this machine is refused even with the local token (the HTTPS
// port listens on the network when remote access is on; a test can only connect from loopback).
void test_r_rtsp_logins::test_routes_need_loopback()
{
    auto api = _api();
    for(auto peer : {"192.168.1.20", "10.0.0.5", "172.16.3.4", "8.8.8.8", "fe80::1", "2001:db8::1", ""})
    {
        bool refused = false;
        try { require_local_caller(peer, LOCAL_TOKEN, api); }
        catch(const r_http_403_exception&) { refused = true; }
        RTF_ASSERT(refused);
    }
    for(auto peer : {"127.0.0.1", "::1"})
        require_local_caller(peer, LOCAL_TOKEN, api);       // does not throw
    // And from this machine, a session token is still refused.
    bool refused = false;
    try { require_local_caller("127.0.0.1", SESSION_TOKEN, api); }
    catch(const r_http_403_exception&) { refused = true; }
    RTF_ASSERT(refused);
}

// The list has user names and names, never a password.
void test_r_rtsp_logins::test_list_has_no_passwords()
{
    api_fixture fx;
    auto a = json::parse(fx.call(METHOD_POST, "/rtsp_logins", LOCAL_TOKEN, R"({"name":"Living room"})").body);
    auto b = json::parse(fx.call(METHOD_POST, "/rtsp_logins", LOCAL_TOKEN, R"({"name":"Kitchen"})").body);
    auto listed = fx.call(METHOD_GET, "/rtsp_logins", LOCAL_TOKEN);
    RTF_ASSERT_EQUAL(listed.status, 200);
    RTF_ASSERT(listed.body.find(a["password"].get<string>()) == string::npos);
    RTF_ASSERT(listed.body.find(b["password"].get<string>()) == string::npos);
    auto j = json::parse(listed.body);
    RTF_ASSERT_EQUAL(j["logins"].size(), (size_t)2);
    for(auto& l : j["logins"])
    {
        RTF_ASSERT_EQUAL(l.size(), (size_t)2);
        RTF_ASSERT(l.contains("username") && l.contains("name"));
    }
    RTF_ASSERT(j["logins"][0]["name"] == "Kitchen" || j["logins"][1]["name"] == "Kitchen");
}

// 32 logins at a time: the 33rd is refused, and a removed one frees its place.
void test_r_rtsp_logins::test_cap()
{
    api_fixture fx;
    string first;
    for(size_t i = 0; i < r_rtsp_logins::MAX_LOGINS; ++i)
    {
        auto r = fx.call(METHOD_POST, "/rtsp_logins", LOCAL_TOKEN, R"({"name":"c"})");
        RTF_ASSERT_EQUAL(r.status, 200);
        if(first.empty())
            first = json::parse(r.body)["username"].get<string>();
    }
    RTF_ASSERT_EQUAL(fx.call(METHOD_POST, "/rtsp_logins", LOCAL_TOKEN, R"({"name":"one too many"})").status, 403);
    RTF_ASSERT_EQUAL(fx.rtsp.logins->list().size(), r_rtsp_logins::MAX_LOGINS);
    RTF_ASSERT_EQUAL(fx.call(METHOD_DELETE, "/rtsp_logins/" + first, LOCAL_TOKEN).status, 204);
    RTF_ASSERT_EQUAL(fx.call(METHOD_POST, "/rtsp_logins", LOCAL_TOKEN, R"({"name":"fits now"})").status, 200);
}
