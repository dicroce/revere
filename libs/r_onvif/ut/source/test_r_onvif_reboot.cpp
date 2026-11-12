#include "test_r_onvif_reboot.h"
#include "r_onvif/r_onvif_session.h"
#include "r_http/r_web_server.h"
#include "r_utils/r_socket.h"
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>

using namespace std;
using namespace r_onvif;
using namespace r_utils;
using namespace r_http;

REGISTER_TEST_FIXTURE(test_r_onvif_reboot);

namespace
{

string _envelope(const string& body)
{
    return "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
           "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" xmlns:tt=\"http://www.onvif.org/ver10/schema\" "
           "xmlns:ter=\"http://www.onvif.org/ver10/error\"><s:Body>" + body + "</s:Body></s:Envelope>";
}

string _fault(const string& subcode, const string& text)
{
    return "<s:Fault><s:Code><s:Value>s:Sender</s:Value><s:Subcode><s:Value>" + subcode + "</s:Value></s:Subcode></s:Code>"
           "<s:Reason><s:Text xml:lang=\"en\">" + text + "</s:Text></s:Reason></s:Fault>";
}

const string NOT_AUTHORIZED = _fault("ter:NotAuthorized", "Sender not authorized");
const string UNSUPPORTED = _fault("ter:ActionNotSupported", "Action Not Implemented");

// How the fake answers one operation.
enum class reply { ok, refuse, unsupported, hang };

// An ONVIF device service on loopback. Each operation answers as `replies` says (ok when not named), and every
// request is counted, with whether it carried a UsernameToken.
struct fake_device
{
    mutex lock;
    map<string, reply> replies;
    map<string, int> calls;
    map<string, int> with_credentials;
    string reboot_message {"Rebooting in 30 seconds"};
    r_web_server<r_socket> ws;

    explicit fake_device(int port) : ws(port, "127.0.0.1")
    {
        ws.add_route(METHOD_POST, "/onvif/device_service", [this](const r_web_server<r_socket>&, r_socket&, const r_server_request& req) {
            return _answer(req.get_body_as_string());
        });
        ws.start();
        for(int i = 0; i < 200; ++i)
        {
            try { r_socket probe; probe.connect("127.0.0.1", port); probe.close(); return; }
            catch(...) { this_thread::sleep_for(chrono::milliseconds(10)); }
        }
    }
    ~fake_device() { ws.stop(); }

    void set(const string& op, reply r) { lock_guard<mutex> g(lock); replies[op] = r; }
    int count(const string& op) { lock_guard<mutex> g(lock); return calls[op]; }
    int count_with_credentials(const string& op) { lock_guard<mutex> g(lock); return with_credentials[op]; }

    static r_server_response _soap(const string& body, status_code status = response_ok)
    {
        r_server_response response(status);
        response.set_content_type("application/soap+xml");
        response.set_body(_envelope(body));
        return response;
    }

    r_server_response _answer(const string& body)
    {
        string op = "other";
        for(auto o : {"GetSystemDateAndTime", "GetUsers", "GetDeviceInformation", "SystemReboot"})
            if(body.find(o) != string::npos) { op = o; break; }
        reply r;
        {
            lock_guard<mutex> g(lock);
            ++calls[op];
            if(body.find("UsernameToken") != string::npos)
                ++with_credentials[op];
            r = replies.count(op) ? replies[op] : reply::ok;
        }
        if(op == "GetSystemDateAndTime")
            return _soap("<tds:GetSystemDateAndTimeResponse><tds:SystemDateAndTime><tt:UTCDateTime>"
                         "<tt:Date><tt:Year>2026</tt:Year><tt:Month>11</tt:Month><tt:Day>11</tt:Day></tt:Date>"
                         "<tt:Time><tt:Hour>20</tt:Hour><tt:Minute>0</tt:Minute><tt:Second>0</tt:Second></tt:Time>"
                         "</tt:UTCDateTime></tds:SystemDateAndTime></tds:GetSystemDateAndTimeResponse>");
        if(r == reply::refuse)
            return _soap(NOT_AUTHORIZED, response_bad_request);
        if(r == reply::unsupported)
            return _soap(UNSUPPORTED, response_bad_request);
        if(r == reply::hang)
        {
            // Goes down without answering, as a camera that reboots before its reply is out.
            this_thread::sleep_for(chrono::milliseconds(REBOOT_TIMEOUT_MS + 1500));
            return _soap("<tds:SystemRebootResponse/>");
        }
        if(op == "GetUsers")
            return _soap("<tds:GetUsersResponse><tds:User><tt:Username>admin</tt:Username><tt:UserLevel>Administrator</tt:UserLevel></tds:User></tds:GetUsersResponse>");
        if(op == "GetDeviceInformation")
            return _soap("<tds:GetDeviceInformationResponse><tds:Manufacturer>Acme</tds:Manufacturer></tds:GetDeviceInformationResponse>");
        if(op == "SystemReboot")
            return _soap("<tds:SystemRebootResponse><tds:Message>" + reboot_message + "</tds:Message></tds:SystemRebootResponse>");
        return _soap("<tds:" + op + "Response/>");
    }
};

r_onvif_cam _cam(int port, bool credentials = true)
{
    return r_onvif_cam("127.0.0.1", port, "http", "/onvif/device_service",
                       credentials ? r_nullable<string>("admin") : r_nullable<string>(),
                       credentials ? r_nullable<string>("pw") : r_nullable<string>());
}

}

void test_r_onvif_reboot::setup()
{
    r_raw_socket::socket_startup();
}

void test_r_onvif_reboot::teardown()
{
}

void test_r_onvif_reboot::test_answers()
{
    RTF_ASSERT(onvif_answer_of(200, _envelope("<tds:SystemRebootResponse/>")) == onvif_answer::ok);
    // A 200 carrying a fault is a fault.
    RTF_ASSERT(onvif_answer_of(200, _envelope(NOT_AUTHORIZED)) == onvif_answer::refused_auth);
    RTF_ASSERT(onvif_answer_of(200, _envelope(UNSUPPORTED)) == onvif_answer::unsupported);
    RTF_ASSERT(onvif_answer_of(401, "") == onvif_answer::refused_auth);
    RTF_ASSERT(onvif_answer_of(400, _envelope(NOT_AUTHORIZED)) == onvif_answer::refused_auth);
    RTF_ASSERT(onvif_answer_of(400, _envelope(UNSUPPORTED)) == onvif_answer::unsupported);
    RTF_ASSERT(onvif_answer_of(400, _envelope(_fault("ter:OptionalActionNotImplemented", "x"))) == onvif_answer::unsupported);
    RTF_ASSERT(onvif_answer_of(500, _envelope(_fault("ter:InvalidArgVal", "bad"))) == onvif_answer::failed);
    RTF_ASSERT(onvif_answer_of(503, "") == onvif_answer::failed);
}

void test_r_onvif_reboot::test_verdicts()
{
    auto is = [](const reboot_check& c, reboot_support s, const string& reason) { return c.support == s && c.reason == reason; };

    RTF_ASSERT(is(reboot_check_of(onvif_answer::ok), reboot_support::supported, "admin"));
    RTF_ASSERT(is(reboot_check_of(onvif_answer::unsupported), reboot_support::unknown, "no_user_list"));
    RTF_ASSERT(is(reboot_check_of(onvif_answer::refused_auth, onvif_answer::ok), reboot_support::not_supported, "not_admin"));
    RTF_ASSERT(is(reboot_check_of(onvif_answer::refused_auth, onvif_answer::refused_auth), reboot_support::unknown, "credentials_rejected"));
    RTF_ASSERT(is(reboot_check_of(onvif_answer::refused_auth, onvif_answer::failed), reboot_support::unknown, "error"));
    RTF_ASSERT(is(reboot_check_of(onvif_answer::refused_auth), reboot_support::unknown, "error"));
    RTF_ASSERT(is(reboot_check_of(onvif_answer::failed), reboot_support::unknown, "error"));

    RTF_ASSERT(is(reboot_check_of_reboot(onvif_answer::ok), reboot_support::supported, "rebooted"));
    RTF_ASSERT(is(reboot_check_of_reboot(onvif_answer::unsupported), reboot_support::not_supported, "unsupported"));
    RTF_ASSERT(is(reboot_check_of_reboot(onvif_answer::refused_auth), reboot_support::not_supported, "refused"));
    RTF_ASSERT(is(reboot_check_of_reboot(onvif_answer::failed), reboot_support::unknown, "error"));

    RTF_ASSERT_EQUAL(string(reboot_support_name(reboot_support::supported)), string("supported"));
    RTF_ASSERT_EQUAL(string(reboot_support_name(reboot_support::not_supported)), string("not_supported"));
    RTF_ASSERT_EQUAL(string(reboot_support_name(reboot_support::unknown)), string("unknown"));
}

void test_r_onvif_reboot::test_check_against_a_camera()
{
    fake_device dev(11140);

    // Administrator credentials: GetUsers works, with the credentials sent. Nothing is rebooted.
    auto ok = _cam(11140).check_reboot_support();
    RTF_ASSERT(ok.support == reboot_support::supported);
    RTF_ASSERT_EQUAL(ok.reason, string("admin"));
    RTF_ASSERT_EQUAL(dev.count_with_credentials("GetUsers"), 1);
    RTF_ASSERT_EQUAL(dev.count("GetDeviceInformation"), 0);
    RTF_ASSERT_EQUAL(dev.count("SystemReboot"), 0);

    // Credentials that work but lack the rights.
    dev.set("GetUsers", reply::refuse);
    auto user = _cam(11140).check_reboot_support();
    RTF_ASSERT(user.support == reboot_support::not_supported);
    RTF_ASSERT_EQUAL(user.reason, string("not_admin"));
    RTF_ASSERT_EQUAL(dev.count_with_credentials("GetDeviceInformation"), 1);

    // Credentials the camera rejects outright: unknown, asked again later.
    dev.set("GetDeviceInformation", reply::refuse);
    auto rejected = _cam(11140).check_reboot_support();
    RTF_ASSERT(rejected.support == reboot_support::unknown);
    RTF_ASSERT_EQUAL(rejected.reason, string("credentials_rejected"));

    // A camera without GetUsers: only a reboot can tell.
    dev.set("GetUsers", reply::unsupported);
    auto no_list = _cam(11140).check_reboot_support();
    RTF_ASSERT(no_list.support == reboot_support::unknown);
    RTF_ASSERT_EQUAL(no_list.reason, string("no_user_list"));

    RTF_ASSERT_EQUAL(dev.count("SystemReboot"), 0);
}

void test_r_onvif_reboot::test_no_credentials_sends_nothing()
{
    fake_device dev(11141);

    auto check = _cam(11141, false).check_reboot_support();
    RTF_ASSERT(check.support == reboot_support::not_supported);
    RTF_ASSERT_EQUAL(check.reason, string("no_credentials"));

    auto result = _cam(11141, false).system_reboot();
    RTF_ASSERT(!result.sent);
    RTF_ASSERT(result.check.support == reboot_support::not_supported);
    RTF_ASSERT_EQUAL(result.check.reason, string("no_credentials"));

    // A user name with no password, or an empty user name, isn't stored credentials either.
    r_onvif_cam no_password("127.0.0.1", 11141, "http", "/onvif/device_service", r_nullable<string>("admin"), r_nullable<string>());
    RTF_ASSERT(!no_password.system_reboot().sent);
    r_onvif_cam empty_user("127.0.0.1", 11141, "http", "/onvif/device_service", r_nullable<string>(""), r_nullable<string>("pw"));
    RTF_ASSERT(!empty_user.system_reboot().sent);

    // This firmware would reboot without credentials; nothing was sent to it.
    RTF_ASSERT_EQUAL(dev.count("GetUsers"), 0);
    RTF_ASSERT_EQUAL(dev.count("GetDeviceInformation"), 0);
    RTF_ASSERT_EQUAL(dev.count("SystemReboot"), 0);
}

void test_r_onvif_reboot::test_reboot_against_a_camera()
{
    fake_device dev(11142);

    auto done = _cam(11142).system_reboot();
    RTF_ASSERT(done.sent);
    RTF_ASSERT(done.check.support == reboot_support::supported);
    RTF_ASSERT_EQUAL(done.check.reason, string("rebooted"));
    RTF_ASSERT_EQUAL(done.message, string("Rebooting in 30 seconds"));
    RTF_ASSERT_EQUAL(dev.count("SystemReboot"), 1);
    RTF_ASSERT_EQUAL(dev.count_with_credentials("SystemReboot"), 1);

    // The camera's message is cut to REBOOT_MAX_MESSAGE.
    dev.reboot_message = string(500, 'x');
    RTF_ASSERT_EQUAL(_cam(11142).system_reboot().message.size(), REBOOT_MAX_MESSAGE);

    dev.set("SystemReboot", reply::refuse);
    auto refused = _cam(11142).system_reboot();
    RTF_ASSERT(refused.sent);
    RTF_ASSERT(refused.check.support == reboot_support::not_supported);
    RTF_ASSERT_EQUAL(refused.check.reason, string("refused"));
    RTF_ASSERT(refused.message.empty());

    dev.set("SystemReboot", reply::unsupported);
    auto unsupported = _cam(11142).system_reboot();
    RTF_ASSERT(unsupported.check.support == reboot_support::not_supported);
    RTF_ASSERT_EQUAL(unsupported.check.reason, string("unsupported"));
}

void test_r_onvif_reboot::test_reboot_without_an_answer_is_sent_once()
{
    fake_device dev(11143);
    dev.set("SystemReboot", reply::hang);

    auto result = _cam(11143).system_reboot();
    RTF_ASSERT(result.sent);
    RTF_ASSERT(result.check.support == reboot_support::unknown);
    RTF_ASSERT_EQUAL(result.check.reason, string("no_answer"));
    // No second request: the camera may be rebooting already.
    RTF_ASSERT_EQUAL(dev.count("SystemReboot"), 1);
}
