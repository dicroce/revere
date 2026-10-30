
#include "test_r_ptz_session.h"
#include "r_disco/providers/r_onvif_provider.h"
#include "r_http/r_web_server.h"
#include "r_utils/r_socket.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace r_disco;
using namespace r_http;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_ptz_session);

// A camera's ONVIF services on loopback: answers the calls r_onvif_provider::ptz_control
// makes, counts each, and can fail the next PTZ calls on request.
namespace
{

int _next_port()
{
    static std::atomic<int> port {23417};
    return port++;
}

string _envelope(const string& body)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:tt=\"http://www.onvif.org/ver10/schema\""
           " xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\""
           " xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\"><s:Body>" + body + "</s:Body></s:Envelope>";
}

struct fake_camera
{
    int port;
    bool has_ptz;
    std::mutex lock;
    map<string, int> calls;
    vector<string> profile_tokens;    // the ProfileToken of each PTZ call, in order
    std::atomic<int> fail_ptz {0};    // fail this many PTZ calls next
    std::atomic<int> fail_caps {0};   // fail this many GetCapabilities next
    r_web_server<r_socket> ws;

    fake_camera(bool ptz = true, int at_port = 0) : port(at_port ? at_port : _next_port()), has_ptz(ptz), ws(port, "127.0.0.1")
    {
        for(auto path : {"/onvif/device_service", "/onvif/media_service", "/onvif/ptz_service"})
            ws.add_route(METHOD_POST, path, [this](const r_web_server<r_socket>&, r_socket&, const r_server_request& req) {
                return _answer(req.get_body_as_string());
            });
        ws.start();
        // start() returns before the server listens: wait until it takes a connection.
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
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    ~fake_camera() { ws.stop(); }

    int count(const string& op)
    {
        std::lock_guard<std::mutex> g(lock);
        return calls[op];
    }

    r_server_response _soap(const string& body, status_code status = response_ok)
    {
        r_server_response response(status);
        response.set_content_type("application/soap+xml");
        response.set_body(_envelope(body));
        return response;
    }

    r_server_response _answer(const string& body)
    {
        static const char* ops[] = {"GetSystemDateAndTime", "GetCapabilities", "GetProfiles", "GetPresets", "ContinuousMove", "tptz:Stop", "GotoPreset"};
        string op;
        for(auto o : ops)
            if(body.find(o) != string::npos) { op = o; break; }

        bool is_ptz_call = op == "GetPresets" || op == "ContinuousMove" || op == "tptz:Stop" || op == "GotoPreset";
        {
            std::lock_guard<std::mutex> g(lock);
            ++calls[op];
            if(is_ptz_call)
            {
                auto at = body.find("ProfileToken>");
                auto end = at == string::npos ? string::npos : body.find('<', at);
                profile_tokens.push_back(end == string::npos ? string() : body.substr(at + 13, end - at - 13));
            }
        }

        if((is_ptz_call && fail_ptz > 0 && fail_ptz-- > 0) || (op == "GetCapabilities" && fail_caps > 0 && fail_caps-- > 0))
        {
            return _soap("<s:Fault><s:Code><s:Value>s:Receiver</s:Value></s:Code><s:Reason><s:Text xml:lang=\"en\">busy</s:Text></s:Reason></s:Fault>",
                         response_internal_server_error);
        }

        auto base = "http://127.0.0.1:" + to_string(port);
        if(op == "GetSystemDateAndTime")
            return _soap("<tds:GetSystemDateAndTimeResponse><tds:SystemDateAndTime><tt:DateTimeType>NTP</tt:DateTimeType>"
                         "</tds:SystemDateAndTime></tds:GetSystemDateAndTimeResponse>");
        if(op == "GetCapabilities")
            return _soap("<tds:GetCapabilitiesResponse><tds:Capabilities>"
                         "<tt:Media><tt:XAddr>" + base + "/onvif/media_service</tt:XAddr></tt:Media>" +
                         (has_ptz ? "<tt:PTZ><tt:XAddr>" + base + "/onvif/ptz_service</tt:XAddr></tt:PTZ>" : string()) +
                         "</tds:Capabilities></tds:GetCapabilitiesResponse>");
        if(op == "GetProfiles")
            return _soap("<trt:GetProfilesResponse><trt:Profiles token=\"profile_1\"><tt:Name>main</tt:Name></trt:Profiles>"
                         "<trt:Profiles token=\"profile_2\"><tt:Name>minor</tt:Name></trt:Profiles></trt:GetProfilesResponse>");
        if(op == "GetPresets")
            return _soap("<tptz:GetPresetsResponse><tptz:Preset token=\"1\"><tt:Name>Gate</tt:Name></tptz:Preset>"
                         "<tptz:Preset token=\"2\"><tt:Name>Porch</tt:Name></tptz:Preset></tptz:GetPresetsResponse>");
        if(op == "ContinuousMove")
            return _soap("<tptz:ContinuousMoveResponse/>");
        if(op == "tptz:Stop")
            return _soap("<tptz:StopResponse/>");
        if(op == "GotoPreset")
            return _soap("<tptz:GotoPresetResponse/>");
        return _soap("", response_bad_request);
    }
};

r_nullable<string> _s(const string& v)
{
    r_nullable<string> n;
    n.set_value(v);
    return n;
}

r_nullable<int> _i(int v)
{
    r_nullable<int> n;
    n.set_value(v);
    return n;
}

r_ptz_command _cmd(r_ptz_command::op_t op)
{
    r_ptz_command c;
    c.op = op;
    if(op == r_ptz_command::op_t::move)
    {
        c.pan = 0.5f;
        c.timeout = "PT2S";
    }
    return c;
}

vector<r_onvif::onvif_ptz_preset> _run(r_onvif_provider& p, fake_camera& cam, const r_ptz_command& cmd, const string& user = "admin")
{
    return p.ptz_control("127.0.0.1", "/onvif/device_service", _s(user), _s("pw"), cmd, _i(cam.port), _s("http"));
}

string _code_of(r_onvif_provider& p, fake_camera& cam, const r_ptz_command& cmd)
{
    try
    {
        _run(p, cam, cmd);
    }
    catch(const r_ptz_error& ex)
    {
        return ex.code();
    }
    return "none";
}

}

void test_r_ptz_session::test_session_kept_between_commands()
{
    fake_camera cam;
    r_onvif_provider p("", nullptr);

    auto presets = _run(p, cam, _cmd(r_ptz_command::op_t::list_presets));
    RTF_ASSERT_EQUAL(presets.size(), (size_t)2);
    RTF_ASSERT(presets[0].token == "1" && presets[0].name == "Gate");

    _run(p, cam, _cmd(r_ptz_command::op_t::move));
    _run(p, cam, _cmd(r_ptz_command::op_t::move));
    _run(p, cam, _cmd(r_ptz_command::op_t::stop));

    // The first command found the camera, its PTZ service and profile; the rest were one request each.
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 1);
    RTF_ASSERT_EQUAL(cam.count("GetProfiles"), 1);
    RTF_ASSERT_EQUAL(cam.count("GetPresets"), 1);
    RTF_ASSERT_EQUAL(cam.count("ContinuousMove"), 2);
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 1);
    // Every PTZ call used the camera's first profile.
    for(auto& t : cam.profile_tokens)
        RTF_ASSERT_EQUAL(t, string("profile_1"));
}

void test_r_ptz_session::test_named_profile_is_used()
{
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    auto cmd = _cmd(r_ptz_command::op_t::stop);
    cmd.profile_token = "profile_2";
    _run(p, cam, cmd);
    _run(p, cam, _cmd(r_ptz_command::op_t::stop));
    RTF_ASSERT_EQUAL(cam.profile_tokens.size(), (size_t)2);
    RTF_ASSERT_EQUAL(cam.profile_tokens[0], string("profile_2"));
    RTF_ASSERT_EQUAL(cam.profile_tokens[1], string("profile_1"));
}

void test_r_ptz_session::test_no_ptz_service()
{
    fake_camera cam(false);
    r_onvif_provider p("", nullptr);
    RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::list_presets)), string("no_ptz"));
    // No session is kept for a camera without PTZ, and asking again isn't retried within one call.
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 1);
    RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::list_presets)), string("no_ptz"));
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 2);
}

void test_r_ptz_session::test_stale_session_opened_again_once()
{
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    _run(p, cam, _cmd(r_ptz_command::op_t::stop));

    // The kept session's next call fails once (the camera rebooted, say): a new session, and the command again.
    cam.fail_ptz = 1;
    RTF_ASSERT_NO_THROW(_run(p, cam, _cmd(r_ptz_command::op_t::move)));
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 2);
    RTF_ASSERT_EQUAL(cam.count("ContinuousMove"), 2);

    // Failing again after the new session: the caller hears about it, and the session isn't kept.
    cam.fail_ptz = 2;
    RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::move)), string("camera_error"));
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 3);
    RTF_ASSERT_EQUAL(cam.count("ContinuousMove"), 4);
    _run(p, cam, _cmd(r_ptz_command::op_t::stop));
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 4);
}

void test_r_ptz_session::test_new_session_failure_not_retried()
{
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    cam.fail_ptz = 1;
    RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::list_presets)), string("camera_error"));
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 1);
    RTF_ASSERT_EQUAL(cam.count("GetPresets"), 1);
    // The failed session isn't kept: the next command starts over.
    _run(p, cam, _cmd(r_ptz_command::op_t::stop));
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 2);
}

void test_r_ptz_session::test_tapo_port_found()
{
    // A Tapo answers ONVIF on 2020: found with no stored port, after the other usual ports.
    fake_camera cam(true, 2020);
    r_onvif_provider p("", nullptr);
    RTF_ASSERT_NO_THROW(p.ptz_control("127.0.0.1", "/onvif/device_service", _s("admin"), _s("pw"),
                                      _cmd(r_ptz_command::op_t::stop), r_nullable<int>(), r_nullable<string>()));
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 1);
}

void test_r_ptz_session::test_new_credentials_new_session()
{
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    _run(p, cam, _cmd(r_ptz_command::op_t::stop), "admin");
    _run(p, cam, _cmd(r_ptz_command::op_t::stop), "admin");
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 1);
    _run(p, cam, _cmd(r_ptz_command::op_t::stop), "operator");
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 2);
}

void test_r_ptz_session::test_cameras_have_own_sessions()
{
    // Two cameras at one address, on different ports: each has its own session, and each command reaches its camera.
    fake_camera a, b;
    r_onvif_provider p("", nullptr);
    _run(p, a, _cmd(r_ptz_command::op_t::stop));
    _run(p, b, _cmd(r_ptz_command::op_t::move));
    _run(p, a, _cmd(r_ptz_command::op_t::stop));
    RTF_ASSERT_EQUAL(a.count("GetCapabilities"), 1);
    RTF_ASSERT_EQUAL(b.count("GetCapabilities"), 1);
    RTF_ASSERT_EQUAL(a.count("tptz:Stop"), 2);
    RTF_ASSERT_EQUAL(a.count("ContinuousMove"), 0);
    RTF_ASSERT_EQUAL(b.count("ContinuousMove"), 1);
}

void test_r_ptz_session::test_open_failure_is_camera_error()
{
    // The camera refuses GetCapabilities: a camera_error, and no session kept, so the next command starts over.
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    cam.fail_caps = 1;
    RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::stop)), string("camera_error"));
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 0);
    _run(p, cam, _cmd(r_ptz_command::op_t::stop));
    RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 2);
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 1);
}
