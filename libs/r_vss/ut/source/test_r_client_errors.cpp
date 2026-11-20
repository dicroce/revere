#include "test_r_client_errors.h"
#include "r_vss/r_ws_client_errors.h"
#include "r_vss/r_query.h"
#include "r_disco/r_devices.h"
#include "r_http/r_web_server.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_logger.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

using namespace r_vss;
using namespace r_http;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_client_errors);

static const char* const TOP = "client_errors_top";

void test_r_client_errors::setup()
{
    teardown();
}

void test_r_client_errors::teardown()
{
    std::filesystem::remove_all(TOP);
}

namespace
{
// A handler body as r_ws writes them: its own catch-all, after R_WS_PASS_CLIENT_ERRORS. Returns the status the web
// server would answer with (r_http_exception's own; 500 for anything else that escaped).
int _status_of(const std::function<void()>& body)
{
    try
    {
        try
        {
            body();
        }
        R_WS_PASS_CLIENT_ERRORS
        catch(const std::exception&)
        {
        }
        R_STHROW(r_http_500_exception, ("Failed."));
    }
    catch(const r_http_exception& e) { return e.get_status(); }
    catch(const r_not_found_exception&) { return 404; }
    catch(const r_invalid_argument_exception&) { return 400; }
    return -1;
}

struct devices_fixture
{
    unique_ptr<r_disco::r_devices> devices;
    devices_fixture()
    {
        std::filesystem::create_directories(TOP);
        devices = make_unique<r_disco::r_devices>(TOP);
        devices->start();
        r_disco::r_camera cam;
        cam.id = "cam-known";
        cam.state = "assigned";
        cam.rtsp_url.set_value("rtsp://10.0.0.93/stream1");
        devices->save_camera(cam);
    }
    ~devices_fixture() { devices->stop(); }
};

struct server_fixture
{
    int port;
    r_web_server<r_socket> ws;
    server_fixture() : port(RTF_NEXT_PORT()), ws(port, "127.0.0.1")
    {
        auto route = [this](const string& path, std::function<void()> f){
            ws.add_route(METHOD_GET, path, [f](const r_web_server<r_socket>&, r_socket&, const r_server_request&) -> r_server_response {
                f();
                return r_server_response();
            });
        };
        route("/missing", [](){ R_STHROW(r_http_400_exception, ("Missing camera_id.")); });
        route("/unknown", [](){ R_STHROW(r_not_found_exception, ("Unknown camera id: nope")); });
        route("/badtime", [](){ r_time_utils::iso_8601_to_tp("not a time"); });
        route("/full", [](){ throw r_http_exception(response_insufficient_storage, "Not enough free disk space: it needs 9 GB and 1 GB is free."); });
        route("/fault", [](){ R_STHROW(r_http_500_exception, ("Failed.")); });
        ws.start();
        for(int i = 0; i < 200; ++i)
        {
            try { r_socket probe; probe.connect("127.0.0.1", port); probe.close(); return; }
            catch(...) { this_thread::sleep_for(milliseconds(10)); }
        }
    }
    ~server_fixture() { ws.stop(); }

    pair<int, string> get(const string& path, string* content_type = nullptr)
    {
        r_client_request request("127.0.0.1", port);
        request.set_method(METHOD_GET);
        request.set_uri(path);
        r_socket sok;
        sok.connect("127.0.0.1", port);
        request.write_request(sok);
        r_client_response response;
        response.read_response(sok);
        auto b = response.get_body_as_string();
        if(content_type)
        {
            *content_type = response.get_header("Content-Type");
        }
        return {response.get_status(), b.is_null() ? string() : b.value()};
    }
};
}

void test_r_client_errors::test_handlers_pass_client_errors_on()
{
    // What reaches the web server from a handler's try: a client's mistake keeps its 4xx; anything else is 500.
    RTF_ASSERT_EQUAL(_status_of([](){ R_STHROW(r_http_400_exception, ("Missing camera_id.")); }), 400);
    RTF_ASSERT_EQUAL(_status_of([](){ R_STHROW(r_http_404_exception, ("Unknown camera id: x")); }), 404);
    RTF_ASSERT_EQUAL(_status_of([](){ R_STHROW(r_not_found_exception, ("Unknown camera id: x")); }), 404);
    RTF_ASSERT_EQUAL(_status_of([](){ r_time_utils::iso_8601_to_tp("yesterday"); }), 400);
    RTF_ASSERT_EQUAL(_status_of([](){ (void)nlohmann::json::parse("{\"camera_id\":"); }), 400);
    RTF_ASSERT_EQUAL(_status_of([](){ nlohmann::json j = {{"byte_rate", "fast"}}; (void)j["byte_rate"].get<int64_t>(); }), 400);
    RTF_ASSERT_EQUAL(_status_of([](){ (void)r_string_utils::s_to_int64("twelve"); }), 400);
    RTF_ASSERT_EQUAL(_status_of([](){ throw r_http_exception(response_insufficient_storage, "full"); }), 507);
    // A fault of the server's own is still 500.
    RTF_ASSERT_EQUAL(_status_of([](){ R_THROW(("Blob tree missing frames array.")); }), 500);
    RTF_ASSERT_EQUAL(_status_of([](){ R_STHROW(r_http_500_exception, ("Failed.")); }), 500);
}

void test_r_client_errors::test_a_bad_time_is_a_bad_argument()
{
    RTF_ASSERT_THROWS(r_time_utils::iso_8601_to_tp("not a time"), r_invalid_argument_exception);
    RTF_ASSERT_THROWS(r_time_utils::iso_8601_to_tp(""), r_invalid_argument_exception);
    // A good one still parses.
    RTF_ASSERT(r_time_utils::iso_8601_to_tp("2026-11-20T08:00:00.000Z").time_since_epoch().count() > 0);
}

void test_r_client_errors::test_queries_say_not_found()
{
    devices_fixture fx;
    auto now = std::chrono::system_clock::now();
    RTF_ASSERT_THROWS(query_get_contents(TOP, *fx.devices, "cam-nope", now - hours(1), now), r_not_found_exception);
    RTF_ASSERT_THROWS(query_get_motion_events(TOP, *fx.devices, "cam-nope", now - hours(1), now), r_not_found_exception);
    // A camera it has, with no recording: not found either (no recording file), and no motion yet is no events.
    RTF_ASSERT_THROWS(query_get_contents(TOP, *fx.devices, "cam-known", now - hours(1), now), r_not_found_exception);
    RTF_ASSERT(query_get_motion_events(TOP, *fx.devices, "cam-known", now - hours(1), now).empty());
}

void test_r_client_errors::test_a_full_disk_is_507()
{
    // A recording file bigger than the disk (100 MB/s for 100,000 days): refused with 507, saying how much it needs.
    devices_fixture fx;
    int status = 0;
    string why;
    try
    {
        query_configure_camera(TOP, *fx.devices, "cam-known", r_nullable<string>(), r_nullable<string>(), "Front", false, 100000.0, 100LL * 1024 * 1024);
    }
    catch(const r_http_exception& e)
    {
        status = e.get_status();
        why = e.what();
    }
    RTF_ASSERT_EQUAL(status, 507);
    RTF_ASSERT(why.find("Not enough free disk space") != string::npos);
    RTF_ASSERT(why.find("is free") != string::npos);
}

void test_r_client_errors::test_server_answers_client_errors_with_json()
{
    server_fixture fx;
    string ct;
    auto r = fx.get("/missing", &ct);
    RTF_ASSERT_EQUAL(r.first, 400);
    RTF_ASSERT_EQUAL(ct, string("application/json"));
    RTF_ASSERT_EQUAL(nlohmann::json::parse(r.second)["error"].get<string>(), string("Missing camera_id."));

    r = fx.get("/unknown");
    RTF_ASSERT_EQUAL(r.first, 404);
    RTF_ASSERT_EQUAL(nlohmann::json::parse(r.second)["error"].get<string>(), string("Unknown camera id: nope"));

    r = fx.get("/badtime");
    RTF_ASSERT_EQUAL(r.first, 400);
    RTF_ASSERT(nlohmann::json::parse(r.second)["error"].get<string>().find("ISO 8601") != string::npos);

    r = fx.get("/full");
    RTF_ASSERT_EQUAL(r.first, 507);
    RTF_ASSERT(nlohmann::json::parse(r.second)["error"].get<string>().find("Not enough free disk space") != string::npos);

    r = fx.get("/fault");
    RTF_ASSERT_EQUAL(r.first, 500);
}

void test_r_client_errors::test_client_errors_log_one_line_each_rate_limited()
{
    // A client hammering bad requests: each is answered, but at most CLIENT_ERROR_LOG_BURST lines in a window, and
    // no line carries a stack trace.
    server_fixture fx;
    std::atomic<int> lines{0}, multi_line{0};
    r_logger::set_log_callback([&](r_logger::LOG_LEVEL, const string& msg){
        if(msg.find("Missing camera_id.") != string::npos)
        {
            ++lines;
            if(msg.find('\n') != string::npos && msg.find('\n') + 1 < msg.size())
                ++multi_line;
        }
    });
    int answered = 0;
    for(int i = 0; i < 60; ++i)
        if(fx.get("/missing").first == 400)
            ++answered;
    r_logger::clear_log_callback();
    RTF_ASSERT_EQUAL(answered, 60);
    RTF_ASSERT(lines.load() >= 1);
    RTF_ASSERT(lines.load() <= CLIENT_ERROR_LOG_BURST);
    RTF_ASSERT_EQUAL(multi_line.load(), 0);
}
