
#include "test_r_ptz_session.h"
#include "r_disco/providers/r_onvif_provider.h"
#include "r_http/r_web_server.h"
#include "r_utils/r_socket.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
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
    string nodes {"fault"};           // how GetNodes answers (see _answer); set before the first call
    string configs {"fault"};         // how GetConfigurations answers: "fault", "unsupported", "pan_only", "both" or "none"
    bool presets_unsupported {false}; // GetPresets answers ActionNotSupported
    bool move_unsupported {false};    // ContinuousMove answers ActionNotSupported
    bool offers_rotate {true};        // GetVideoSourceConfigurationOptions lists Rotate ON and OFF
    string rotate {"OFF"};            // the source's Rotate Mode, as SetVideoSourceConfiguration last set it
    string degree;                    // its Rotate Degree, likewise; empty: none
    string rotate_attrs;              // Rotate's attributes as GetProfile gives them, e.g. ` Mirror="true"`
    string more_extension;            // the rest of the source's Extension, after Rotate
    bool rotate_reboot {false};       // its options say a Rotate change restarts the camera
    bool offers_on {true};            // its Rotate options include ON (else OFF only)
    string vsc_name {"\n  Source 1\n"}; // the VideoSourceConfiguration's Name, as GetProfile gives it
    map<string, string> last_body;    // the last request body of each op
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
        // GetProfiles before GetProfile: the first's body holds the second's name.
        static const char* ops[] = {"GetSystemDateAndTime", "GetCapabilities", "GetProfiles", "GetPresets", "ContinuousMove", "tptz:Stop", "GotoPreset", "GetNodes", "GetConfigurations",
                                    "GetVideoSourceConfigurationOptions", "SetVideoSourceConfiguration", "GetProfile"};
        string op;
        for(auto o : ops)
            if(body.find(o) != string::npos) { op = o; break; }

        bool is_ptz_call = op == "GetPresets" || op == "ContinuousMove" || op == "tptz:Stop" || op == "GotoPreset";
        {
            std::lock_guard<std::mutex> g(lock);
            ++calls[op];
            last_body[op] = body;
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
        if(op == "GetPresets" && presets_unsupported)
            return _unsupported();
        if(op == "GetPresets")
            return _soap("<tptz:GetPresetsResponse><tptz:Preset token=\"1\"><tt:Name>Gate</tt:Name></tptz:Preset>"
                         "<tptz:Preset token=\"2\"><tt:Name>Porch</tt:Name></tptz:Preset></tptz:GetPresetsResponse>");
        if(op == "ContinuousMove" && move_unsupported)
            return _unsupported();
        if(op == "ContinuousMove")
            return _soap("<tptz:ContinuousMoveResponse/>");
        if(op == "tptz:Stop")
            return _soap("<tptz:StopResponse/>");
        if(op == "GetProfile")
        {
            std::lock_guard<std::mutex> g(lock);
            return _soap("<trt:GetProfileResponse><trt:Profile token=\"profile_1\"><tt:Name>main</tt:Name>"
                         "<tt:VideoSourceConfiguration token=\"vsc_1\"><tt:Name>" + vsc_name + "</tt:Name><tt:UseCount>2</tt:UseCount>"
                         "<tt:SourceToken> source_1 </tt:SourceToken><tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
                         "<tt:Extension><tt:Rotate" + rotate_attrs + "><tt:Mode>" + rotate + "</tt:Mode>" +
                         (degree.empty() ? "" : "<tt:Degree>" + degree + "</tt:Degree>") + "</tt:Rotate>" + more_extension + "</tt:Extension>"
                         "</tt:VideoSourceConfiguration></trt:Profile></trt:GetProfileResponse>");
        }
        if(op == "GetVideoSourceConfigurationOptions")
            return _soap(string("<trt:GetVideoSourceConfigurationOptionsResponse><trt:Options>"
                         "<tt:BoundsRange><tt:XRange><tt:Min>0</tt:Min><tt:Max>0</tt:Max></tt:XRange></tt:BoundsRange>") +
                         (offers_rotate ? string("<tt:Extension><tt:Rotate") + (rotate_reboot ? " Reboot=\"true\"" : "") +
                                          "><tt:Mode>OFF</tt:Mode>" + (offers_on ? "<tt:Mode>ON</tt:Mode>" : "") + "</tt:Rotate></tt:Extension>" : "") +
                         "</trt:Options></trt:GetVideoSourceConfigurationOptionsResponse>");
        if(op == "SetVideoSourceConfiguration")
        {
            std::lock_guard<std::mutex> g(lock);
            // Rotate's Mode comes first; its Degree, if sent.
            smatch m;
            rotate = regex_search(body, m, regex("<tt:Mode>(ON|OFF)</tt:Mode>")) ? m[1].str() : "OFF";
            if(regex_search(body, m, regex("<tt:Degree>([0-9]+)</tt:Degree>")))
                degree = m[1].str();
            return _soap("<trt:SetVideoSourceConfigurationResponse/>");
        }
        if(op == "GotoPreset")
            return _soap("<tptz:GotoPresetResponse/>");
        // GetNodes per nodes: "fault", "unsupported" (ActionNotSupported), "no_spaces" (no SupportedPTZSpaces),
        // "no_continuous" (spaces, none a continuous pan, tilt or zoom one), "pan_only" or "both".
        if(op == "GetNodes")
        {
            if(nodes == "fault")
                return _soap("", response_bad_request);
            if(nodes == "unsupported")
                return _unsupported();
            string spaces = nodes == "no_spaces" ? string()
                : nodes == "no_continuous" ? string("<tt:SupportedPTZSpaces><tt:AbsolutePanTiltPositionSpace/></tt:SupportedPTZSpaces>")
                : "<tt:SupportedPTZSpaces><tt:ContinuousPanTiltVelocitySpace/>" +
                  string(nodes == "both" ? "<tt:ContinuousZoomVelocitySpace/>" : "") + "</tt:SupportedPTZSpaces>";
            return _soap("<tptz:GetNodesResponse><tptz:PTZNode token=\"n1\">" + spaces + "</tptz:PTZNode></tptz:GetNodesResponse>");
        }
        if(op == "GetConfigurations")
        {
            if(configs == "fault")
                return _soap("", response_bad_request);
            if(configs == "unsupported")
                return _unsupported();
            if(configs == "none")
                return _soap("<tptz:GetConfigurationsResponse><tptz:PTZConfiguration token=\"c1\"><tt:Name>PTZ</tt:Name>"
                             "</tptz:PTZConfiguration></tptz:GetConfigurationsResponse>");
            return _soap("<tptz:GetConfigurationsResponse><tptz:PTZConfiguration token=\"c1\"><tt:Name>PTZ</tt:Name>"
                         "<tt:DefaultContinuousPanTiltVelocitySpace>x</tt:DefaultContinuousPanTiltVelocitySpace>" +
                         string(configs == "both" ? "<tt:DefaultContinuousZoomVelocitySpace>x</tt:DefaultContinuousZoomVelocitySpace>" : "") +
                         "</tptz:PTZConfiguration></tptz:GetConfigurationsResponse>");
        }
        return _soap("", response_bad_request);
    }

    // What a camera answers to something it doesn't do.
    r_server_response _unsupported()
    {
        return _soap("<s:Fault><s:Code><s:Value>s:Sender</s:Value><s:Subcode><s:Value>ter:ActionNotSupported</s:Value></s:Subcode></s:Code>"
                     "<s:Reason><s:Text xml:lang=\"en\">Action not supported</s:Text></s:Reason></s:Fault>", response_bad_request);
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

r_ptz_result _run(r_onvif_provider& p, fake_camera& cam, const r_ptz_command& cmd, const string& user = "admin")
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

    auto presets = _run(p, cam, _cmd(r_ptz_command::op_t::list_presets)).presets;
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

// list_presets reports the axes the camera's PTZ node names (GetNodes).
void test_r_ptz_session::test_list_presets_reports_axes()
{
    {
        fake_camera cam;
        cam.nodes = "pan_only";
        r_onvif_provider p("", nullptr);
        auto r = _run(p, cam, _cmd(r_ptz_command::op_t::list_presets));
        RTF_ASSERT(!r.supports_pan_tilt.is_null() && r.supports_pan_tilt.value());
        RTF_ASSERT(!r.supports_zoom.is_null() && !r.supports_zoom.value());
        RTF_ASSERT_EQUAL(r.presets.size(), (size_t)2);
    }
    {
        fake_camera cam;
        cam.nodes = "both";
        r_onvif_provider p("", nullptr);
        auto r = _run(p, cam, _cmd(r_ptz_command::op_t::list_presets));
        RTF_ASSERT(!r.supports_zoom.is_null() && r.supports_zoom.value());
    }
}

// When the camera doesn't say (GetNodes faults, or names no SupportedPTZSpaces)
// the flags are null, not false, and list_presets still answers. Other ops
// carry no flags.
void test_r_ptz_session::test_axes_unknown_are_null()
{
    for(auto how : {"fault", "no_spaces"})
    {
        fake_camera cam;
        cam.nodes = how;
        r_onvif_provider p("", nullptr);
        auto r = _run(p, cam, _cmd(r_ptz_command::op_t::list_presets));
        RTF_ASSERT(r.supports_pan_tilt.is_null());
        RTF_ASSERT(r.supports_zoom.is_null());
        RTF_ASSERT_EQUAL(r.presets.size(), (size_t)2);
    }
    fake_camera cam;
    cam.nodes = "both";
    r_onvif_provider p("", nullptr);
    auto moved = _run(p, cam, _cmd(r_ptz_command::op_t::move));
    RTF_ASSERT(moved.supports_pan_tilt.is_null() && moved.supports_zoom.is_null());
}

// A camera that advertises a PTZ service is PTZ only if a node (GetNodes) or a configuration (GetConfigurations) lists a
// pan, tilt or zoom space. One that has none answers "no_ptz", and is then not asked again.
void test_r_ptz_session::test_a_ptz_service_without_axes_is_not_ptz()
{
    // GetNodes lists spaces and none is a continuous one; GetConfigurations says the action isn't supported.
    {
        fake_camera cam;
        cam.nodes = "no_continuous";
        cam.configs = "unsupported";
        r_onvif_provider p("", nullptr);
        RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::list_presets)), string("no_ptz"));
        RTF_ASSERT_EQUAL(cam.count("GetNodes"), 1);
        // Every command after it answers at once, without a call to the camera.
        for(auto op : {r_ptz_command::op_t::list_presets, r_ptz_command::op_t::move, r_ptz_command::op_t::stop, r_ptz_command::op_t::goto_preset})
            RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(op)), string("no_ptz"));
        RTF_ASSERT_EQUAL(cam.count("GetNodes"), 1);
        RTF_ASSERT_EQUAL(cam.count("GetCapabilities"), 1);
        RTF_ASSERT_EQUAL(cam.count("GetPresets") + cam.count("ContinuousMove") + cam.count("tptz:Stop") + cam.count("GotoPreset"), 0);
    }
    // ActionNotSupported to both probes.
    {
        fake_camera cam;
        cam.nodes = "unsupported";
        cam.configs = "unsupported";
        r_onvif_provider p("", nullptr);
        RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::list_presets)), string("no_ptz"));
        RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::move)), string("no_ptz"));
        RTF_ASSERT_EQUAL(cam.count("GetNodes"), 1);
        RTF_ASSERT_EQUAL(cam.count("ContinuousMove"), 0);
    }
    // Nodes say nothing about axes and neither do the configurations.
    {
        fake_camera cam;
        cam.nodes = "no_spaces";
        cam.configs = "none";
        r_onvif_provider p("", nullptr);
        RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::list_presets)), string("no_ptz"));
    }
}

// The configurations settle it when the nodes can't: a camera whose GetNodes isn't supported but whose configuration has
// a pan/tilt space is PTZ, with the axes the configuration shows.
void test_r_ptz_session::test_the_configurations_settle_what_the_nodes_cannot()
{
    fake_camera cam;
    cam.nodes = "unsupported";
    cam.configs = "pan_only";
    r_onvif_provider p("", nullptr);
    auto r = _run(p, cam, _cmd(r_ptz_command::op_t::list_presets));
    RTF_ASSERT(!r.supports_pan_tilt.is_null() && r.supports_pan_tilt.value());
    RTF_ASSERT(!r.supports_zoom.is_null() && !r.supports_zoom.value());
    RTF_ASSERT_EQUAL(r.presets.size(), (size_t)2);
    // The nodes' answer is kept: a later list is no new probe.
    _run(p, cam, _cmd(r_ptz_command::op_t::list_presets));
    RTF_ASSERT_EQUAL(cam.count("GetNodes"), 1);
    RTF_ASSERT_EQUAL(cam.count("GetConfigurations"), 1);
}

// A probe that fails in a way that says nothing about PTZ (a camera that is busy, a bad login) is not "no PTZ": the
// camera is treated as before, and asked again next time.
void test_r_ptz_session::test_a_failed_probe_is_not_remembered()
{
    fake_camera cam;
    cam.nodes = "unsupported";
    cam.configs = "fault";
    r_onvif_provider p("", nullptr);
    auto r = _run(p, cam, _cmd(r_ptz_command::op_t::list_presets));
    RTF_ASSERT_EQUAL(r.presets.size(), (size_t)2);
    RTF_ASSERT(r.supports_pan_tilt.is_null());
    // Nothing was remembered as "none": a move goes to the camera.
    _run(p, cam, _cmd(r_ptz_command::op_t::move));
    RTF_ASSERT_EQUAL(cam.count("ContinuousMove"), 1);
}

// ActionNotSupported to GetPresets is a camera with PTZ and no presets, not a camera with no PTZ.
void test_r_ptz_session::test_no_presets_feature_is_still_ptz()
{
    fake_camera cam;
    cam.nodes = "both";
    cam.presets_unsupported = true;
    r_onvif_provider p("", nullptr);
    auto r = _run(p, cam, _cmd(r_ptz_command::op_t::list_presets));
    RTF_ASSERT(r.presets.empty());
    RTF_ASSERT(!r.supports_zoom.is_null() && r.supports_zoom.value());
    _run(p, cam, _cmd(r_ptz_command::op_t::move));
    RTF_ASSERT_EQUAL(cam.count("ContinuousMove"), 1);
}

// A camera that passed the probe and then says a command isn't supported has no PTZ to speak of: "no_ptz", and the calls
// stop. How long that is remembered is a plain function of the clock.
void test_r_ptz_session::test_an_unsupported_command_stops_the_calls()
{
    fake_camera cam;
    cam.nodes = "both";
    cam.move_unsupported = true;
    r_onvif_provider p("", nullptr);
    RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::move)), string("no_ptz"));
    RTF_ASSERT_EQUAL(cam.count("ContinuousMove"), 1);
    // Not tried again, and not for the other commands either.
    RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::move)), string("no_ptz"));
    RTF_ASSERT_EQUAL(_code_of(p, cam, _cmd(r_ptz_command::op_t::list_presets)), string("no_ptz"));
    RTF_ASSERT_EQUAL(cam.count("ContinuousMove"), 1);
    RTF_ASSERT_EQUAL(cam.count("GetPresets"), 0);
    RTF_ASSERT_EQUAL(cam.count("GetNodes"), 1);

    RTF_ASSERT(!ptz_none_valid(0, 1000));
    RTF_ASSERT(ptz_none_valid(1000, 1000));
    RTF_ASSERT(ptz_none_valid(1000, 1000 + PTZ_NONE_TTL_MS - 1));
    RTF_ASSERT(!ptz_none_valid(1000, 1000 + PTZ_NONE_TTL_MS));
    RTF_ASSERT(!ptz_none_valid(1000, 999));      // a clock that went back doesn't extend it
}

// A move sends only the axes it uses: a camera without zoom can fault on a Zoom
// velocity, even 0, while it pans.
void test_r_ptz_session::test_only_the_axes_in_use()
{
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    auto body = [&]() { std::lock_guard<std::mutex> g(cam.lock); return cam.last_body["ContinuousMove"]; };

    auto pan = _cmd(r_ptz_command::op_t::move);   // pan 0.5
    _run(p, cam, pan);
    RTF_ASSERT(body().find("PanTilt") != string::npos);
    RTF_ASSERT(body().find("Zoom") == string::npos);

    auto zoom = _cmd(r_ptz_command::op_t::move);
    zoom.pan = 0.0f;
    zoom.zoom = -0.5f;
    _run(p, cam, zoom);
    RTF_ASSERT(body().find("Zoom") != string::npos);
    RTF_ASSERT(body().find("PanTilt") == string::npos);

    auto both = _cmd(r_ptz_command::op_t::move);
    both.tilt = 0.5f;
    both.zoom = 0.5f;
    _run(p, cam, both);
    RTF_ASSERT(body().find("PanTilt") != string::npos && body().find("Zoom") != string::npos);

    // No axis at all still names PanTilt (Velocity needs one).
    auto none = _cmd(r_ptz_command::op_t::move);
    none.pan = 0.0f;
    _run(p, cam, none);
    RTF_ASSERT(body().find("PanTilt") != string::npos);
    RTF_ASSERT(body().find("Zoom") == string::npos);
}

// Stop names no axis: without PanTilt or Zoom it stops all movement (ONVIF PTZ).
void test_r_ptz_session::test_stop_names_pan_tilt()
{
    // A Tapo keeps moving on a Stop that doesn't name PanTilt; Zoom stays out, for cameras without zoom.
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    _run(p, cam, _cmd(r_ptz_command::op_t::stop));
    string stop;
    {
        std::lock_guard<std::mutex> g(cam.lock);
        stop = cam.last_body["tptz:Stop"];
    }
    RTF_ASSERT(stop.find("tptz:Stop") != string::npos);
    RTF_ASSERT(stop.find("<tptz:PanTilt>true</tptz:PanTilt>") != string::npos);
    RTF_ASSERT(stop.find("Zoom") == string::npos);
}

namespace
{
r_ptz_command _move(const string& timeout)
{
    auto c = _cmd(r_ptz_command::op_t::move);
    c.timeout = timeout;
    return c;
}
void _wait_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}

void test_r_ptz_session::test_timeout_read()
{
    RTF_ASSERT_EQUAL(ptz_timeout_ms("PT1S").value(), (int64_t)1000);
    RTF_ASSERT_EQUAL(ptz_timeout_ms("PT1.5S").value(), (int64_t)1500);
    RTF_ASSERT_EQUAL(ptz_timeout_ms("PT0.25S").value(), (int64_t)250);
    RTF_ASSERT_EQUAL(ptz_timeout_ms("PT1M2S").value(), (int64_t)62000);
    RTF_ASSERT_EQUAL(ptz_timeout_ms("PT1H").value(), (int64_t)3600000);
    RTF_ASSERT_EQUAL(ptz_timeout_ms("PT1H1M1.5S").value(), (int64_t)3661500);
    for(auto bad : {"", "PT", "PT0S", "1S", "PT1D", "PTxS", "P1S", "PT1S1M", "PT1.5H"})
        RTF_ASSERT(ptz_timeout_ms(bad).is_null());

    // The bounds: a request's timeout is read as it came.
    RTF_ASSERT_EQUAL(ptz_timeout_ms("PT24H").value(), PTZ_TIMEOUT_MAX_MS);
    RTF_ASSERT_EQUAL(ptz_timeout_ms("PT0.001S").value(), (int64_t)1);
    for(auto bad : {"PT24H0.001S", "PT25H", "PT1441M", "PT0.0001S", "PT99999999999999999999H"})
        RTF_ASSERT(ptz_timeout_ms(bad).is_null());
    // Too long to read at all: a run that overflowed stod (out_of_range), and one padded past the limit.
    RTF_ASSERT(ptz_timeout_ms("PT" + string(400, '9') + "S").is_null());
    RTF_ASSERT(ptz_timeout_ms("PT" + string(PTZ_TIMEOUT_MAX_CHARS, '0') + "1S").is_null());
    RTF_ASSERT(ptz_timeout_ms("PT" + string(PTZ_TIMEOUT_MAX_CHARS - 4, '0') + "1S").value() == 1000);
}

void test_r_ptz_session::test_move_stopped_at_its_timeout()
{
    // The camera ignores the Timeout (as a Tapo does), and the client's Stop never comes: Revere stops it.
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    _run(p, cam, _move("PT0.3S"));
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 0);
    _wait_ms(800);
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 1);
    // Once: nothing more after it.
    _wait_ms(500);
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 1);
}

void test_r_ptz_session::test_later_command_cancels_the_stop()
{
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    // A client's Stop after the move: no second one from Revere.
    _run(p, cam, _move("PT0.3S"));
    _run(p, cam, _cmd(r_ptz_command::op_t::stop));
    _wait_ms(800);
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 1);
    // A keep-alive (a held control) takes over the first move's Stop: the camera stops only at the last one's.
    _run(p, cam, _move("PT0.6S"));
    _wait_ms(300);
    _run(p, cam, _move("PT0.6S"));
    _wait_ms(450);
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 1);
    _wait_ms(600);
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 2);
}

void test_r_ptz_session::test_keep_alives_leave_one_stop_waiting()
{
    fake_camera cam;
    fake_camera other;
    r_onvif_provider p("", nullptr);
    // A held control: each keep-alive replaces the last one's Stop, rather than adding another.
    for(int i = 0; i < 5; ++i)
        _run(p, cam, _move("PT30S"));
    RTF_ASSERT_EQUAL(p.pending_ptz_stops(), (size_t)1);
    // Another camera's move has its own.
    _run(p, other, _move("PT30S"));
    RTF_ASSERT_EQUAL(p.pending_ptz_stops(), (size_t)2);
    // A client's Stop leaves none for that camera.
    _run(p, cam, _cmd(r_ptz_command::op_t::stop));
    RTF_ASSERT_EQUAL(p.pending_ptz_stops(), (size_t)1);
}

void test_r_ptz_session::test_no_timeout_no_stop()
{
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    _run(p, cam, _move(""));
    _wait_ms(500);
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 0);
}

void test_r_ptz_session::test_shutdown_drops_pending_stops()
{
    fake_camera cam;
    {
        r_onvif_provider p("", nullptr);
        _run(p, cam, _move("PT30S"));
    }
    // Gone at once, without waiting out the 30 s or stopping the camera.
    RTF_ASSERT_EQUAL(cam.count("tptz:Stop"), 0);
}

namespace
{
r_imaging_command _flip(bool on)
{
    r_imaging_command c;
    c.op = r_imaging_command::op_t::set_flip;
    c.flip = on;
    return c;
}
r_onvif::onvif_video_flip _imaging(r_onvif_provider& p, fake_camera& cam, const r_imaging_command& cmd)
{
    return p.imaging_control("127.0.0.1", "/onvif/device_service", _s("admin"), _s("pw"), cmd, _i(cam.port), _s("http"));
}
}

void test_r_ptz_session::test_flip_read()
{
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    auto f = _imaging(p, cam, r_imaging_command());
    RTF_ASSERT(f.supported);
    RTF_ASSERT(!f.on);
    RTF_ASSERT(!f.reboot);
    // A read sets nothing.
    RTF_ASSERT_EQUAL(cam.count("SetVideoSourceConfiguration"), 0);
}

void test_r_ptz_session::test_flip_set_sends_the_source_back_with_rotate()
{
    fake_camera cam;
    r_onvif_provider p("", nullptr);
    auto f = _imaging(p, cam, _flip(true));
    RTF_ASSERT(f.supported && f.on);
    string set;
    {
        std::lock_guard<std::mutex> g(cam.lock);
        set = cam.last_body["SetVideoSourceConfiguration"];
    }
    // The source's own configuration, trimmed, with only its rotation changed, kept over a reboot.
    RTF_ASSERT(set.find("token=\"vsc_1\"") != string::npos);
    RTF_ASSERT(set.find("<tt:Name>Source 1</tt:Name>") != string::npos);
    RTF_ASSERT(set.find("<tt:UseCount>2</tt:UseCount>") != string::npos);
    RTF_ASSERT(set.find("<tt:SourceToken>source_1</tt:SourceToken>") != string::npos);
    RTF_ASSERT(set.find("width=\"1920\"") != string::npos && set.find("height=\"1080\"") != string::npos);
    RTF_ASSERT(set.find("<tt:Rotate><tt:Mode>ON</tt:Mode></tt:Rotate>") != string::npos);
    RTF_ASSERT(set.find("<trt:ForcePersistence>true</trt:ForcePersistence>") != string::npos);
    // Already on: nothing to set. Off again: set.
    _imaging(p, cam, _flip(true));
    RTF_ASSERT_EQUAL(cam.count("SetVideoSourceConfiguration"), 1);
    RTF_ASSERT(!_imaging(p, cam, _flip(false)).on);
    RTF_ASSERT_EQUAL(cam.count("SetVideoSourceConfiguration"), 2);
}

void test_r_ptz_session::test_flip_keeps_the_source_name()
{
    // A Name past 16 bytes (Hikvision and Dahua say "VideoSourceConfig", 17) goes back whole: a flip renames nothing.
    fake_camera cam;
    cam.vsc_name = "VideoSourceConfig_Main_Sensor_1";
    r_onvif_provider p("", nullptr);
    _imaging(p, cam, _flip(true));
    string set;
    {
        std::lock_guard<std::mutex> g(cam.lock);
        set = cam.last_body["SetVideoSourceConfiguration"];
    }
    RTF_ASSERT(set.find("<tt:Name>VideoSourceConfig_Main_Sensor_1</tt:Name>") != string::npos);
    // A Name longer than ONVIF allows isn't cut and sent back: the set is refused, and nothing is sent.
    fake_camera odd;
    odd.vsc_name = string(65, 'n');
    r_onvif_provider q("", nullptr);
    string code;
    try { _imaging(q, odd, _flip(true)); }
    catch(const r_ptz_error& ex) { code = ex.code(); }
    RTF_ASSERT_EQUAL(code, string("camera_error"));
    RTF_ASSERT_EQUAL(odd.count("SetVideoSourceConfiguration"), 0);
}

void test_r_ptz_session::test_flip_unsupported()
{
    fake_camera cam;
    cam.offers_rotate = false;
    r_onvif_provider p("", nullptr);
    RTF_ASSERT(!_imaging(p, cam, r_imaging_command()).supported);
    string code;
    try { _imaging(p, cam, _flip(true)); }
    catch(const r_ptz_error& ex) { code = ex.code(); }
    RTF_ASSERT_EQUAL(code, string("unsupported"));
    RTF_ASSERT_EQUAL(cam.count("SetVideoSourceConfiguration"), 0);
}

void test_r_ptz_session::test_flip_needs_no_ptz()
{
    // A camera with no PTZ service can still flip its picture.
    fake_camera cam(false);
    r_onvif_provider p("", nullptr);
    RTF_ASSERT(_imaging(p, cam, _flip(true)).on);
}

void test_r_ptz_session::test_flip_keeps_the_extension()
{
    // The camera's own Extension goes back whole: Rotate's Mirror and the SceneOrientation it set, only Mode changed.
    fake_camera cam;
    cam.rotate_attrs = " Mirror=\"true\"";
    cam.more_extension = "<tt:Extension><tt:SceneOrientation><tt:Mode>MANUAL</tt:Mode><tt:Orientation>Below</tt:Orientation></tt:SceneOrientation></tt:Extension>";
    r_onvif_provider p("", nullptr);
    RTF_ASSERT(_imaging(p, cam, _flip(true)).on);
    string set;
    {
        std::lock_guard<std::mutex> g(cam.lock);
        set = cam.last_body["SetVideoSourceConfiguration"];
    }
    RTF_ASSERT(set.find("<tt:Extension><tt:Rotate Mirror=\"true\"><tt:Mode>ON</tt:Mode></tt:Rotate>"
                        "<tt:Extension><tt:SceneOrientation><tt:Mode>MANUAL</tt:Mode><tt:Orientation>Below</tt:Orientation></tt:SceneOrientation></tt:Extension>"
                        "</tt:Extension>") != string::npos);
    RTF_ASSERT(!_imaging(p, cam, _flip(false)).on);
    {
        std::lock_guard<std::mutex> g(cam.lock);
        set = cam.last_body["SetVideoSourceConfiguration"];
    }
    RTF_ASSERT(set.find("<tt:Rotate Mirror=\"true\"><tt:Mode>OFF</tt:Mode></tt:Rotate>") != string::npos);
    RTF_ASSERT(set.find("<tt:Orientation>Below</tt:Orientation>") != string::npos);
}

void test_r_ptz_session::test_flip_is_180_degrees()
{
    // Turned 90 degrees: Rotate ON, but not flipped. Flipping it sets Degree 180 with Mode ON.
    fake_camera cam;
    cam.rotate = "ON";
    cam.degree = "90";
    r_onvif_provider p("", nullptr);
    auto f = _imaging(p, cam, r_imaging_command());
    RTF_ASSERT(f.supported && !f.on);
    RTF_ASSERT(_imaging(p, cam, _flip(true)).on);
    string set;
    {
        std::lock_guard<std::mutex> g(cam.lock);
        set = cam.last_body["SetVideoSourceConfiguration"];
    }
    RTF_ASSERT(set.find("<tt:Rotate><tt:Mode>ON</tt:Mode><tt:Degree>180</tt:Degree></tt:Rotate>") != string::npos);
    // Off changes only the Mode.
    RTF_ASSERT(!_imaging(p, cam, _flip(false)).on);
    {
        std::lock_guard<std::mutex> g(cam.lock);
        set = cam.last_body["SetVideoSourceConfiguration"];
    }
    RTF_ASSERT(set.find("<tt:Rotate><tt:Mode>OFF</tt:Mode><tt:Degree>180</tt:Degree></tt:Rotate>") != string::npos);
    // Degree 180 and ON is flipped; 270 isn't.
    cam.rotate = "ON";
    RTF_ASSERT(_imaging(p, cam, r_imaging_command()).on);
    cam.degree = "270";
    RTF_ASSERT(!_imaging(p, cam, r_imaging_command()).on);
}

void test_r_ptz_session::test_flip_reboot()
{
    // A camera whose options say a Rotate change restarts it: said on a read, and a set answers as set, without
    // asking the restarting camera again.
    fake_camera cam;
    cam.rotate_reboot = true;
    r_onvif_provider p("", nullptr);
    auto f = _imaging(p, cam, r_imaging_command());
    RTF_ASSERT(f.supported && !f.on && f.reboot);
    auto before = cam.count("GetProfile");
    auto set = _imaging(p, cam, _flip(true));
    RTF_ASSERT(set.supported && set.on && set.reboot);
    RTF_ASSERT_EQUAL(cam.count("SetVideoSourceConfiguration"), 1);
    // One GetProfile to read it, one for the set's configuration, none after.
    RTF_ASSERT_EQUAL(cam.count("GetProfile") - before, 2);
    // A camera that offers no flip (Rotate OFF only) says no reboot either.
    fake_camera none;
    none.offers_on = false;
    none.rotate_reboot = true;
    r_onvif_provider q("", nullptr);
    auto n = _imaging(q, none, r_imaging_command());
    RTF_ASSERT(!n.supported && !n.reboot);
}

namespace
{
// append_flip_extension into a request whose envelope binds tt, as the session's does.
string _flip_extension(const string& get_profile_xml, bool on)
{
    pugi::xml_document doc;
    auto env = doc.append_child("SOAP-ENV:Envelope");
    env.append_attribute("xmlns:tt") = "http://www.onvif.org/ver10/schema";
    auto cfg = env.append_child("SOAP-ENV:Body").append_child("trt:Configuration");
    r_onvif::append_flip_extension(cfg, get_profile_xml, on);
    ostringstream out;
    cfg.print(out, "", pugi::format_raw);
    return out.str();
}
const string _profile_head = "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\""
                             " xmlns:ns2=\"http://www.onvif.org/ver10/schema\"><s:Body><trt:GetProfileResponse><trt:Profile token=\"p\">"
                             "<ns2:VideoSourceConfiguration token=\"v\"><ns2:Name>s</ns2:Name>";
const string _profile_tail = "</ns2:VideoSourceConfiguration></trt:Profile></trt:GetProfileResponse></s:Body></s:Envelope>";
}

void test_r_ptz_session::test_flip_extension_keeps_the_cameras_namespaces()
{
    // The camera's own prefix, bound only in its response, is declared on the copy; a vendor element keeps its own.
    auto x = _flip_extension(_profile_head + "<ns2:Extension><ns2:Rotate><ns2:Mode>OFF</ns2:Mode></ns2:Rotate>"
                             "<ns2:Extension><vnd:Lens xmlns:vnd=\"urn:vendor\" vnd:id=\"1\">wide</vnd:Lens></ns2:Extension></ns2:Extension>" + _profile_tail, true);
    RTF_ASSERT_EQUAL(x, string("<trt:Configuration><ns2:Extension xmlns:ns2=\"http://www.onvif.org/ver10/schema\"><ns2:Rotate><ns2:Mode>ON</ns2:Mode></ns2:Rotate>"
                               "<ns2:Extension><vnd:Lens xmlns:vnd=\"urn:vendor\" vnd:id=\"1\">wide</vnd:Lens></ns2:Extension></ns2:Extension></trt:Configuration>"));
    // A default namespace is declared too.
    auto d = _flip_extension(_profile_head + "<Extension xmlns=\"http://www.onvif.org/ver10/schema\"><Rotate><Mode>ON</Mode></Rotate></Extension>" + _profile_tail, false);
    RTF_ASSERT_EQUAL(d, string("<trt:Configuration><Extension xmlns=\"http://www.onvif.org/ver10/schema\"><Rotate><Mode>OFF</Mode></Rotate></Extension></trt:Configuration>"));
}

void test_r_ptz_session::test_flip_extension_added_where_missing()
{
    // No Extension: one with Rotate/Mode. An Extension without Rotate: Rotate goes first in it.
    RTF_ASSERT_EQUAL(_flip_extension(_profile_head + _profile_tail, true),
                     string("<trt:Configuration><tt:Extension><tt:Rotate><tt:Mode>ON</tt:Mode></tt:Rotate></tt:Extension></trt:Configuration>"));
    auto x = _flip_extension(_profile_head + "<ns2:Extension><ns2:Extension><ns2:LensDescription>x</ns2:LensDescription></ns2:Extension></ns2:Extension>" + _profile_tail, true);
    RTF_ASSERT_EQUAL(x, string("<trt:Configuration><ns2:Extension xmlns:ns2=\"http://www.onvif.org/ver10/schema\"><tt:Rotate><tt:Mode>ON</tt:Mode></tt:Rotate>"
                               "<ns2:Extension><ns2:LensDescription>x</ns2:LensDescription></ns2:Extension></ns2:Extension></trt:Configuration>"));
    // A Rotate without Mode gets it first; a Degree is set to 180 only when turned on.
    auto r = _flip_extension(_profile_head + "<ns2:Extension><ns2:Rotate><ns2:Degree>90</ns2:Degree></ns2:Rotate></ns2:Extension>" + _profile_tail, false);
    RTF_ASSERT(r.find("<ns2:Rotate><tt:Mode>OFF</tt:Mode><ns2:Degree>90</ns2:Degree></ns2:Rotate>") != string::npos);
}

namespace
{
// A profile whose Extension holds `levels` elements nested one in another, the innermost with text.
string _nested_extension(int levels)
{
    string open, close;
    for(int i = 0; i < levels; ++i)
    {
        open += "<ns2:Extension>";
        close += "</ns2:Extension>";
    }
    return _profile_head + open + "x" + close + _profile_tail;
}
}

void test_r_ptz_session::test_flip_extension_depth_is_capped()
{
    // The copy recurses once a level, and the XML is the camera's: 32 levels copy, 33 are refused.
    auto ok = _flip_extension(_nested_extension(32), true);
    RTF_ASSERT(ok.find("<tt:Mode>ON</tt:Mode>") != string::npos);
    RTF_ASSERT(ok.find(">x</ns2:Extension>") != string::npos);
    RTF_ASSERT_THROWS(_flip_extension(_nested_extension(33), true), std::runtime_error);
    // Deep enough to run out of stack with no cap: refused the same way.
    RTF_ASSERT_THROWS(_flip_extension(_nested_extension(200000), true), std::runtime_error);
}
