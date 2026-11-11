#include "test_tray_fetch.h"
#include "tray_fetch.h"
#include "tray_status.h"
#include "r_http/r_server_request.h"
#include "r_http/r_server_response.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_file.h"
#include "r_utils/r_local_api_token.h"
#include <atomic>
#include <exception>
#include <string>
#include <thread>

using namespace revere;
using namespace std;
using namespace r_utils;
using namespace r_http;

REGISTER_TEST_FIXTURE(test_tray_fetch);

static const string TOP_DIR = "tray_fetch_top_dir";

void test_tray_fetch::setup()
{
    r_local_api_token::remove(TOP_DIR);
    r_fs::mkdir_p(TOP_DIR);
}

void test_tray_fetch::teardown()
{
    r_local_api_token::remove(TOP_DIR);
}

namespace
{

// A stand-in for Revere's web API: answers one request, as `status`, with
// `body`, and keeps what it was asked.
struct one_shot_server
{
    int port {RTF_NEXT_PORT()};
    string path;
    r_nullable<string> authorization;
    exception_ptr failure;
    thread th;

    one_shot_server(status_code status, const string& body)
    {
        atomic<bool> listening {false};
        th = thread([this, status, body, &listening] {
            try
            {
                r_socket sok;
                sok.bind(port, "127.0.0.1");
                sok.listen();
                listening = true;
                auto conn = sok.accept();
                r_server_request req;
                req.read_request(conn);
                path = req.get_uri().get_full_raw_uri();
                authorization = req.get_header("Authorization");
                r_server_response resp(status, "application/json");
                resp.set_body(body);
                resp.write_response(conn);
            }
            catch(...)
            {
                failure = current_exception();
                listening = true;
            }
        });
        while(!listening)
            this_thread::yield();
    }

    void finish()
    {
        th.join();
        if(failure)
            rethrow_exception(failure);
    }
};

}

void test_tray_fetch::test_sends_the_local_token()
{
    r_local_api_token::write(TOP_DIR, "tok-1");
    one_shot_server srv(response_ok, R"({"cameras":[]})");
    auto reply = fetch_cameras("127.0.0.1", srv.port, TOP_DIR);
    srv.finish();

    RTF_ASSERT(srv.path == "/cameras");
    RTF_ASSERT(!srv.authorization.is_null());
    RTF_ASSERT(srv.authorization.value() == "Bearer tok-1");
    RTF_ASSERT(reply.status == 200);
    RTF_ASSERT(reply.body == R"({"cameras":[]})");
    RTF_ASSERT(summarize_reply(reply).summary == "Revere is running");
    RTF_ASSERT(summarize_reply(reply).detail == "No cameras set up yet");
}

void test_tray_fetch::test_reads_the_token_each_time()
{
    // Revere replaces the token each time it starts; the tray keeps polling.
    r_local_api_token::write(TOP_DIR, "before-restart");
    {
        one_shot_server srv(response_ok, R"({"cameras":[]})");
        fetch_cameras("127.0.0.1", srv.port, TOP_DIR);
        srv.finish();
        RTF_ASSERT(srv.authorization.value() == "Bearer before-restart");
    }
    r_local_api_token::write(TOP_DIR, "after-restart");
    {
        one_shot_server srv(response_ok, R"({"cameras":[]})");
        fetch_cameras("127.0.0.1", srv.port, TOP_DIR);
        srv.finish();
        RTF_ASSERT(srv.authorization.value() == "Bearer after-restart");
    }
}

void test_tray_fetch::test_no_token_file_is_refused_not_down()
{
    // No token to read (say, Revere runs as another user): it goes without one,
    // and Revere's 401 shows as running, not as down.
    one_shot_server srv(response_unauthorized, R"({"error":"unauthorized"})");
    auto reply = fetch_cameras("127.0.0.1", srv.port, TOP_DIR);
    srv.finish();

    RTF_ASSERT(srv.authorization.is_null());
    RTF_ASSERT(reply.status == 401);
    auto st = summarize_reply(reply);
    RTF_ASSERT(st.reachable);
    RTF_ASSERT(st.problem);
    RTF_ASSERT(st.summary == "Revere is running");
    RTF_ASSERT(st.detail == "Can't read its status: no access to its local API token");
}

void test_tray_fetch::test_nothing_listening()
{
    auto reply = fetch_cameras("127.0.0.1", RTF_NEXT_PORT(), TOP_DIR, 1000);
    RTF_ASSERT(reply.status == 0);
    auto st = summarize_reply(reply);
    RTF_ASSERT(!st.reachable);
    RTF_ASSERT(st.summary == "Revere service isn't running");
}
