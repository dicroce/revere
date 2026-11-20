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
        route("/badtime", [](){ request_time("not a time", "start_time"); });
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
    // What reaches the web server from a handler's try: the request's own bad input keeps its 4xx, through the
    // request_* helpers, and so does a resource that isn't there.
    RTF_ASSERT_EQUAL(_status_of([](){ R_STHROW(r_http_400_exception, ("Missing camera_id.")); }), 400);
    RTF_ASSERT_EQUAL(_status_of([](){ R_STHROW(r_http_404_exception, ("Unknown camera id: x")); }), 404);
    RTF_ASSERT_EQUAL(_status_of([](){ R_STHROW(r_not_found_exception, ("Unknown camera id: x")); }), 404);
    RTF_ASSERT_EQUAL(_status_of([](){ (void)request_time("yesterday", "start_time"); }), 400);
    RTF_ASSERT_EQUAL(_status_of([](){ (void)request_number("twelve", "since_ms", r_string_utils::s_to_int64); }), 400);
    RTF_ASSERT_EQUAL(_status_of([](){ (void)request_json("{\"camera_id\":"); }), 400);
    RTF_ASSERT_EQUAL(_status_of([](){ (void)request_json("[1, 2]"); }), 400);
    RTF_ASSERT_EQUAL(_status_of([](){ throw r_http_exception(response_insufficient_storage, "full"); }), 507);

    // The same failures on the server's own data are faults: 500, with the handler's stack trace. A truncated blob
    // tree (r_blob_tree throws r_invalid_argument_exception), a stored codec parameter that isn't a number, a JSON
    // type error on data the server built, a stored time that doesn't parse.
    RTF_ASSERT_EQUAL(_status_of([](){ R_STHROW(r_invalid_argument_exception, ("r_blob_tree: truncated input")); }), 500);
    RTF_ASSERT_EQUAL(_status_of([](){ (void)r_string_utils::s_to_int("forty-eight-thousand"); }), 500);
    RTF_ASSERT_EQUAL(_status_of([](){ nlohmann::json j = {{"byte_rate", "fast"}}; (void)j["byte_rate"].get<int64_t>(); }), 500);
    RTF_ASSERT_EQUAL(_status_of([](){ (void)nlohmann::json::parse("{\"frames\":"); }), 500);
    RTF_ASSERT_EQUAL(_status_of([](){ r_time_utils::iso_8601_to_tp("stored garbage"); }), 500);
    RTF_ASSERT_EQUAL(_status_of([](){ R_THROW(("Blob tree missing frames array.")); }), 500);
    RTF_ASSERT_EQUAL(_status_of([](){ R_STHROW(r_http_500_exception, ("Failed.")); }), 500);
}

void test_r_client_errors::test_request_input_is_a_400()
{
    // The request helpers: a 400 that names the input, or the parsed value.
    string why;
    try { request_time("not a time", "start_time"); } catch(const r_http_400_exception& e) { why = e.what(); }
    RTF_ASSERT(why.find("start_time isn't an ISO 8601 time: not a time") != string::npos);
    RTF_ASSERT_THROWS(request_time("", "end_time"), r_http_400_exception);
    RTF_ASSERT(request_time("2026-11-20T08:00:00.000Z", "start_time") == r_time_utils::iso_8601_to_tp("2026-11-20T08:00:00.000Z"));
    RTF_ASSERT_EQUAL(request_number("640", "width", r_string_utils::s_to_uint16), (uint16_t)640);
    RTF_ASSERT_THROWS(request_number("wide", "width", r_string_utils::s_to_uint16), r_http_400_exception);
    RTF_ASSERT_EQUAL(request_json("{\"camera_id\": \"a\"}")["camera_id"].get<string>(), string("a"));
    RTF_ASSERT_THROWS(request_json(""), r_http_400_exception);
    // A long bad value is cut short in the message.
    why.clear();
    try { request_time(string(500, 'x'), "start_time"); } catch(const r_http_400_exception& e) { why = r_http::first_line(e.what()); }
    RTF_ASSERT(why.size() < 120);
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

    // A camera not interrogated yet (no RTSP URL): 409, its state, not the request, is the problem.
    r_disco::r_camera raw;
    raw.id = "cam-raw";
    raw.state = "assigned";
    fx.devices->save_camera(raw);
    status = 0;
    try
    {
        query_configure_camera(TOP, *fx.devices, "cam-raw", r_nullable<string>(), r_nullable<string>(), "Raw", false, 1.0, 1000);
    }
    catch(const r_http_exception& e)
    {
        status = e.get_status();
    }
    RTF_ASSERT_EQUAL(status, 409);
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
    RTF_ASSERT(nlohmann::json::parse(r.second)["error"].get<string>().find("start_time isn't an ISO 8601 time") != string::npos);

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
