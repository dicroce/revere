#include "test_r_onvif_push_events.h"
#include "r_cameras/onvif/r_onvif_events.h"
#include "r_http/r_web_server.h"
#include "r_utils/r_socket.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace r_cameras;
using namespace r_http;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_onvif_push_events);

namespace
{

string _envelope(const string& body)
{
    return "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" "
           "xmlns:wsa=\"http://www.w3.org/2005/08/addressing\" xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\" "
           "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" xmlns:tt=\"http://www.onvif.org/ver10/schema\"><s:Body>" + body + "</s:Body></s:Envelope>";
}

string _notify(bool motion)
{
    return _envelope(string("<wsnt:Notify><wsnt:NotificationMessage><wsnt:Topic>tns1:RuleEngine/CellMotionDetector/Motion</wsnt:Topic>"
                            "<wsnt:Message><tt:Message UtcTime=\"2026-11-10T20:48:50Z\" PropertyOperation=\"Changed\">"
                            "<tt:Source><tt:SimpleItem Name=\"VideoSourceConfigurationToken\" Value=\"000\"/></tt:Source>"
                            "<tt:Data><tt:SimpleItem Name=\"IsMotion\" Value=\"") + (motion ? "true" : "false") +
                     "\"/></tt:Data></tt:Message></wsnt:Message></wsnt:NotificationMessage></wsnt:Notify>");
}

const char* UNSUPPORTED = "<s:Fault><s:Code><s:Value>s:Receiver</s:Value><s:Subcode><s:Value>ter:ActionNotSupported</s:Value></s:Subcode></s:Code>"
                          "<s:Reason><s:Text xml:lang=\"en\">Action Not Implemented</s:Text></s:Reason></s:Fault>";

struct fake_camera
{
    int port;
    string pull_support;          // the capability's WSPullPointSupport: "false", "true" or "" (not said)
    bool pull_works {false};      // CreatePullPointSubscription works (else it's ActionNotSupported)
    bool subscribe_works {true};  // Subscribe works (else ActionNotSupported)
    bool renew_fails {false};     // Renew answers with a fault (the subscription is gone)
    mutex lock;
    map<string, int> calls;
    int grant_s {0};              // Subscribe says it granted this many seconds (on a clock that is wrong), 0: says nothing
    string consumer;              // the consumer address Subscribe was given
    r_web_server<r_socket> ws;

    explicit fake_camera(int p) : port(p), ws(p, "127.0.0.1")
    {
        for(auto path : {"/onvif/device_service", "/onvif/events", "/onvif/sub1"})
            ws.add_route(METHOD_POST, path, [this](const r_web_server<r_socket>&, r_socket&, const r_server_request& req) {
                return _answer(req.get_body_as_string());
            });
        ws.start();
        for(int i = 0; i < 200; ++i)
        {
            try { r_socket probe; probe.connect("127.0.0.1", port); probe.close(); return; }
            catch(...) { this_thread::sleep_for(chrono::milliseconds(10)); }
        }
    }
    ~fake_camera() { ws.stop(); }

    int count(const string& op)
    {
        lock_guard<mutex> g(lock);
        return calls[op];
    }
    string consumer_url()
    {
        lock_guard<mutex> g(lock);
        return consumer;
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
        string op = "other";
        for(auto o : {"GetSystemDateAndTime", "GetCapabilities", "CreatePullPointSubscription", "PullMessages", "Unsubscribe", "Subscribe", "Renew"})
            if(body.find(o) != string::npos) { op = o; break; }
        lock_guard<mutex> g(lock);
        ++calls[op];
        if(op == "GetSystemDateAndTime")
            return _soap("<tds:GetSystemDateAndTimeResponse/>");
        if(op == "GetCapabilities")
            return _soap("<tds:GetCapabilitiesResponse><tds:Capabilities><tt:Events><tt:XAddr>http://127.0.0.1:" + to_string(port) + "/onvif/events</tt:XAddr>" +
                         (pull_support.empty() ? "" : "<tt:WSPullPointSupport>" + pull_support + "</tt:WSPullPointSupport>") +
                         "</tt:Events></tds:Capabilities></tds:GetCapabilitiesResponse>");
        if(op == "CreatePullPointSubscription")
        {
            if(!pull_works)
                return _soap(UNSUPPORTED, response_bad_request);
            return _soap("<tev:CreatePullPointSubscriptionResponse><tev:SubscriptionReference><wsa:Address>http://127.0.0.1:" + to_string(port) +
                         "/onvif/sub1</wsa:Address></tev:SubscriptionReference></tev:CreatePullPointSubscriptionResponse>");
        }
        if(op == "PullMessages")
            return _soap("<tev:PullMessagesResponse/>");
        if(op == "Subscribe")
        {
            if(!subscribe_works)
                return _soap(UNSUPPORTED, response_bad_request);
            auto a = body.find("<wsa:Address>");
            auto b = body.find("</wsa:Address>");
            if(a != string::npos && b != string::npos)
                consumer = body.substr(a + 13, b - a - 13);
            string times;
            if(grant_s > 0)
            {
                char end[32];
                snprintf(end, sizeof(end), "2026-11-10T20:%02d:%02dZ", 48 + (50 + grant_s) / 60, (50 + grant_s) % 60);
                times = string("<wsnt:CurrentTime>2026-11-10T20:48:50Z</wsnt:CurrentTime><wsnt:TerminationTime>") + end + "</wsnt:TerminationTime>";
            }
            return _soap("<wsnt:SubscribeResponse><wsnt:SubscriptionReference><wsa:Address>http://127.0.0.1:" + to_string(port) +
                         "/onvif/sub1</wsa:Address></wsnt:SubscriptionReference>" + times + "</wsnt:SubscribeResponse>");
        }
        if(op == "Renew" && renew_fails)
            return _soap("<s:Fault><s:Code><s:Value>s:Sender</s:Value></s:Code><s:Reason><s:Text xml:lang=\"en\">gone</s:Text></s:Reason></s:Fault>", response_bad_request);
        return _soap("<wsnt:" + op + "Response/>");
    }
};

// The path and port of a consumer URL: http://127.0.0.1:11040/onvif/notify/<id>
bool _split(const string& url, int& port, string& path)
{
    auto colon = url.rfind(':');
    auto slash = url.find('/', colon);
    if(colon == string::npos || slash == string::npos)
        return false;
    port = atoi(url.substr(colon + 1, slash - colon - 1).c_str());
    path = url.substr(slash);
    return true;
}

int _post_notify(const string& url, const string& body, bool bare_path = false)
{
    int port;
    string path;
    if(!_split(url, port, path))
        return -1;
    if(bare_path)
        path = "/";     // what a camera that keeps only the host and port does
    r_socket s;
    try { s.connect("127.0.0.1", port); } catch(...) { return -2; }     // nothing listening any more
    string req = "POST " + path + " HTTP/1.1\r\nContent-Type: application/soap+xml\r\nContent-Length: " + to_string(body.size()) + "\r\n\r\n" + body;
    s.send(req.data(), req.size());
    uint64_t ms = 3000;
    if(!s.wait_till_recv_wont_block(ms))
        return 0;
    char buf[64] = {0};
    int n = s.recv(buf, sizeof(buf) - 1);
    return n > 9 ? atoi(buf + 9) : 0;
}

template<class F>
bool _until(F f, int ms = 6000)
{
    for(int i = 0; i < ms / 20; ++i)
    {
        if(f())
            return true;
        this_thread::sleep_for(chrono::milliseconds(20));
    }
    return f();
}

struct events_rig
{
    r_disco::r_camera camera;
    mutex lock;
    vector<r_camera_transition> got;
    unique_ptr<r_onvif_events> events;

    explicit events_rig(int camera_port)
    {
        camera.id = "cam";
        camera.ipv4 = string("127.0.0.1");
        camera.onvif_port = camera_port;
        camera.onvif_protocol = string("http");
        events.reset(new r_onvif_events(camera, [this](const r_camera_transition& t) {
            lock_guard<mutex> g(lock);
            got.push_back(t);
        }));
#ifdef IS_WINDOWS
        _putenv_s("REVERE_ONVIF_PUSH_BIND", "127.0.0.1");
#else
        setenv("REVERE_ONVIF_PUSH_BIND", "127.0.0.1", 1);
#endif
        events->start();
    }
    ~events_rig()
    {
        events.reset();
#ifdef IS_WINDOWS
        _putenv_s("REVERE_ONVIF_PUSH_BIND", "");
#else
        unsetenv("REVERE_ONVIF_PUSH_BIND");
#endif
    }
    size_t transitions()
    {
        lock_guard<mutex> g(lock);
        return got.size();
    }
};

}

void test_r_onvif_push_events::test_push_when_the_camera_has_no_pull_point()
{
    fake_camera cam(11110);
    cam.pull_support = "false";
    string url;
    {
        events_rig rig(cam.port);
        RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
        RTF_ASSERT(rig.events->status().up);
        // No pull point was ever asked for, and the camera was given an address of Revere's.
        RTF_ASSERT_EQUAL(cam.count("CreatePullPointSubscription"), 0);
        RTF_ASSERT_EQUAL(cam.count("Subscribe"), 1);
        url = cam.consumer_url();
        RTF_ASSERT(url.find("http://127.0.0.1:") == 0);
        RTF_ASSERT(url.find("/onvif/notify/") != string::npos);

        // Its events reach the same tracker a pull's do.
        RTF_ASSERT_EQUAL(_post_notify(url, _notify(true)), 200);
        RTF_ASSERT(_until([&]() { return rig.transitions() >= 1; }));
        {
            lock_guard<mutex> g(rig.lock);
            RTF_ASSERT_EQUAL(rig.got[0].what, string("motion"));
            RTF_ASSERT(rig.got[0].active);
        }
        RTF_ASSERT_EQUAL(_post_notify(url, _notify(false)), 200);
        RTF_ASSERT(_until([&]() { return rig.transitions() >= 2; }));
        {
            lock_guard<mutex> g(rig.lock);
            RTF_ASSERT(!rig.got[1].active);
        }
        RTF_ASSERT(_until([&]() { return rig.events->status().motion_topic.find("CellMotionDetector") != string::npos; }));
        RTF_ASSERT_EQUAL(rig.events->spans().size(), (size_t)1);
    }
    // Stopping ends the subscription at the camera, and Revere stops taking its address.
    RTF_ASSERT_EQUAL(cam.count("Unsubscribe"), 1);
    int port; string path;
    RTF_ASSERT(_split(url, port, path));
    RTF_ASSERT(_post_notify(url, _notify(true)) != 200);
}

void test_r_onvif_push_events::test_push_when_pull_point_is_not_supported()
{
    // WSPullPointSupport isn't said, and creating a pull point says "not supported": push is used instead.
    fake_camera cam(11111);
    events_rig rig(cam.port);
    RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
    RTF_ASSERT_EQUAL(cam.count("CreatePullPointSubscription"), 1);
    RTF_ASSERT_EQUAL(cam.count("Subscribe"), 1);
    RTF_ASSERT_EQUAL(_post_notify(cam.consumer_url(), _notify(true)), 200);
    RTF_ASSERT(_until([&]() { return rig.transitions() >= 1; }));
}

void test_r_onvif_push_events::test_pull_point_is_used_when_there_is_one()
{
    // Push is the fallback only: a camera with a pull point is pulled, and never subscribed for push.
    fake_camera cam(11112);
    cam.pull_support = "true";
    cam.pull_works = true;
    events_rig rig(cam.port);
    RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
    RTF_ASSERT(_until([&]() { return cam.count("PullMessages") >= 1; }));
    RTF_ASSERT_EQUAL(cam.count("Subscribe"), 0);
}

void test_r_onvif_push_events::test_subscribe_not_supported()
{
    // No pull point and no push: the camera has no events to give, which isn't "down".
    fake_camera cam(11113);
    cam.pull_support = "false";
    cam.subscribe_works = false;
    events_rig rig(cam.port);
    RTF_ASSERT(_until([&]() { return rig.events->status().state == "not supported"; }));
    RTF_ASSERT(rig.events->status().unavailable());
    RTF_ASSERT_EQUAL(cam.count("Subscribe"), 1);
}

void test_r_onvif_push_events::test_junk_notifications_are_dropped()
{
    fake_camera cam(11114);
    cam.pull_support = "false";
    events_rig rig(cam.port);
    RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
    string url = cam.consumer_url();
    // Not XML, and too many messages: answered (the camera isn't left waiting), dropped, and the next good one still works.
    RTF_ASSERT_EQUAL(_post_notify(url, "this is not xml <"), 200);
    string many = "<s:Envelope xmlns:s=\"x\" xmlns:wsnt=\"y\"><s:Body><wsnt:Notify>";
    for(int i = 0; i < 300; ++i)
        many += "<wsnt:NotificationMessage><wsnt:Topic>t</wsnt:Topic></wsnt:NotificationMessage>";
    many += "</wsnt:Notify></s:Body></s:Envelope>";
    RTF_ASSERT_EQUAL(_post_notify(url, many), 200);
    RTF_ASSERT_EQUAL(_post_notify(url, _notify(true)), 200);
    RTF_ASSERT(_until([&]() { return rig.transitions() >= 1; }));
    RTF_ASSERT(rig.events->status().up);
}

void test_r_onvif_push_events::test_the_subscription_is_renewed()
{
    // A subscription that is never renewed expires at the camera and its events stop: it is renewed at half its time.
    r_onvif_events::set_termination_seconds_for_tests(2);
    {
        fake_camera cam(11115);
        cam.pull_support = "false";
        events_rig rig(cam.port);
        RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
        RTF_ASSERT(cam.count("Renew") == 0);
        RTF_ASSERT(_until([&]() { return cam.count("Renew") >= 1; }, 8000));
        RTF_ASSERT(_until([&]() { return cam.count("Renew") >= 2; }, 8000));
        RTF_ASSERT(rig.events->status().up);
        RTF_ASSERT_EQUAL(cam.count("Subscribe"), 1);    // renewed, not subscribed again
    }
    r_onvif_events::set_termination_seconds_for_tests(120);
}

void test_r_onvif_push_events::test_a_camera_that_posts_to_the_root_and_grants_less()
{
    // One camera's way: it grants 4 s however long is asked for (120 s), posts to "/" and not to the address it was given,
    // and repeats "no motion" as a heartbeat. The renewal follows what it granted, and only a change is an event.
    fake_camera cam(11116);
    cam.pull_support = "false";
    cam.grant_s = 4;
    events_rig rig(cam.port);
    RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
    RTF_ASSERT(_until([&]() { return cam.count("Renew") >= 1; }, 5000));    // at 2 s, not at 60 s

    string url = cam.consumer_url();
    for(int i = 0; i < 3; ++i)
        RTF_ASSERT_EQUAL(_post_notify(url, _notify(false), true), 200);
    this_thread::sleep_for(chrono::milliseconds(500));
    RTF_ASSERT_EQUAL(rig.transitions(), (size_t)0);

    RTF_ASSERT_EQUAL(_post_notify(url, _notify(true), true), 200);
    RTF_ASSERT(_until([&]() { return rig.transitions() >= 1; }));
    RTF_ASSERT_EQUAL(_post_notify(url, _notify(false), true), 200);
    RTF_ASSERT(_until([&]() { return rig.transitions() >= 2; }));
    RTF_ASSERT_EQUAL(rig.transitions(), (size_t)2);
    RTF_ASSERT(rig.events->status().up);
}

void test_r_onvif_push_events::test_a_heartbeat_that_stops_resubscribes()
{
    // A camera that was sending a heartbeat and goes quiet has lost the subscription: it is subscribed again.
    r_onvif_events::set_heartbeat_silence_floor_for_tests(1);
    {
        fake_camera cam(11117);
        cam.pull_support = "false";
        events_rig rig(cam.port);
        RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
        string url = cam.consumer_url();
        // The same "no motion" four times, a second apart: a heartbeat of 1 s, and quiet for 4 s is lost.
        for(int i = 0; i < 4; ++i)
        {
            RTF_ASSERT_EQUAL(_post_notify(url, _notify(false), true), 200);
            this_thread::sleep_for(chrono::milliseconds(1100));
        }
        RTF_ASSERT(_until([&]() { return cam.count("Subscribe") >= 2; }, 20000));
        RTF_ASSERT(cam.count("Unsubscribe") >= 1);     // and the lost one was dropped at the camera first
    }
    r_onvif_events::set_heartbeat_silence_floor_for_tests(30);
}

void test_r_onvif_push_events::test_a_camera_that_only_sends_changes_is_left_alone()
{
    // No heartbeat seen (notifications far apart): quiet is normal, and nothing is subscribed again.
    r_onvif_events::set_heartbeat_silence_floor_for_tests(1);
    {
        fake_camera cam(11118);
        cam.pull_support = "false";
        events_rig rig(cam.port);
        RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
        // A short motion: on, then off a second later (changes, never the same twice in a row), and then quiet.
        RTF_ASSERT_EQUAL(_post_notify(cam.consumer_url(), _notify(true), true), 200);
        this_thread::sleep_for(chrono::milliseconds(1100));
        RTF_ASSERT_EQUAL(_post_notify(cam.consumer_url(), _notify(false), true), 200);
        this_thread::sleep_for(chrono::milliseconds(1100));
        RTF_ASSERT_EQUAL(_post_notify(cam.consumer_url(), _notify(true), true), 200);
        this_thread::sleep_for(chrono::milliseconds(1100));
        RTF_ASSERT_EQUAL(_post_notify(cam.consumer_url(), _notify(false), true), 200);
        this_thread::sleep_for(chrono::milliseconds(6000));
        RTF_ASSERT_EQUAL(cam.count("Subscribe"), 1);
        RTF_ASSERT(rig.events->status().up);
    }
    r_onvif_events::set_heartbeat_silence_floor_for_tests(30);
}

void test_r_onvif_push_events::test_a_heartbeat_keeps_it_subscribed()
{
    // A beat every second for longer than the quiet that would count as lost: still the one subscription.
    r_onvif_events::set_heartbeat_silence_floor_for_tests(1);
    {
        fake_camera cam(11119);
        cam.pull_support = "false";
        events_rig rig(cam.port);
        RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
        for(int i = 0; i < 8; ++i)
        {
            RTF_ASSERT_EQUAL(_post_notify(cam.consumer_url(), _notify(false), true), 200);
            this_thread::sleep_for(chrono::milliseconds(1000));
        }
        RTF_ASSERT_EQUAL(cam.count("Subscribe"), 1);
        RTF_ASSERT(rig.events->status().up);
    }
    r_onvif_events::set_heartbeat_silence_floor_for_tests(30);
}

void test_r_onvif_push_events::test_push_stays_chosen_after_a_failure_and_the_subscription_is_dropped()
{
    // Push was chosen because creating a pull point said "not supported". When the subscription is later lost (a Renew that
    // fails), the camera is told to drop it, and push is used again without asking for a pull point.
    r_onvif_events::set_termination_seconds_for_tests(2);
    {
        fake_camera cam(11120);       // no WSPullPointSupport, pull point not supported
        events_rig rig(cam.port);
        RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
        RTF_ASSERT_EQUAL(cam.count("CreatePullPointSubscription"), 1);
        cam.pull_works = true;           // if it were asked again, it would work and be used
        cam.renew_fails = true;
        RTF_ASSERT(_until([&]() { return cam.count("Unsubscribe") >= 1; }, 6000));
        cam.renew_fails = false;
        RTF_ASSERT(_until([&]() { return cam.count("Subscribe") >= 2; }, 20000));
        RTF_ASSERT_EQUAL(cam.count("CreatePullPointSubscription"), 1);
        RTF_ASSERT_EQUAL(cam.count("PullMessages"), 0);
    }
    r_onvif_events::set_termination_seconds_for_tests(120);
}

void test_r_onvif_push_events::test_two_repeats_are_not_a_heartbeat()
{
    // Several in a row are needed: a "no motion" and its repeat, then quiet, isn't a lost heartbeat.
    r_onvif_events::set_heartbeat_silence_floor_for_tests(1);
    {
        fake_camera cam(11121);
        cam.pull_support = "false";
        events_rig rig(cam.port);
        RTF_ASSERT(_until([&]() { return rig.events->status().state == "subscribed"; }));
        for(int i = 0; i < 3; ++i)      // one, and two repeats of it
        {
            RTF_ASSERT_EQUAL(_post_notify(cam.consumer_url(), _notify(false), true), 200);
            this_thread::sleep_for(chrono::milliseconds(1100));
        }
        this_thread::sleep_for(chrono::milliseconds(6000));
        RTF_ASSERT_EQUAL(cam.count("Subscribe"), 1);
        RTF_ASSERT(rig.events->status().up);
    }
    r_onvif_events::set_heartbeat_silence_floor_for_tests(30);
}
