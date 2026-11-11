#include "test_r_onvif_push.h"
#include "r_onvif/r_onvif_push.h"
#include "r_onvif/r_onvif_session.h"
#include "r_http/r_web_server.h"
#include "r_utils/r_socket.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace r_onvif;
using namespace r_utils;
using namespace r_http;

REGISTER_TEST_FIXTURE(test_r_onvif_push);

// A Notify as a camera pushes it (shaped like the pulled messages): one motion change.
static const char* NOTIFY =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
    "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" "
    "xmlns:tt=\"http://www.onvif.org/ver10/schema\" xmlns:tns1=\"http://www.onvif.org/ver10/topics\">"
    "<s:Body><wsnt:Notify><wsnt:NotificationMessage>"
      "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">tns1:RuleEngine/CellMotionDetector/Motion</wsnt:Topic>"
      "<wsnt:Message><tt:Message UtcTime=\"2026-11-10T20:48:50Z\" PropertyOperation=\"Changed\">"
        "<tt:Source><tt:SimpleItem Name=\"VideoSourceConfigurationToken\" Value=\"000\"/></tt:Source>"
        "<tt:Data><tt:SimpleItem Name=\"IsMotion\" Value=\"true\"/></tt:Data>"
      "</tt:Message></wsnt:Message>"
    "</wsnt:NotificationMessage></wsnt:Notify></s:Body></s:Envelope>";

static string _post(const string& path, const string& body, const string& extra_headers = "")
{
    return "POST " + path + " HTTP/1.1\r\nHost: x\r\nContent-Type: application/soap+xml\r\n" + extra_headers +
           "Content-Length: " + to_string(body.size()) + "\r\n\r\n" + body;
}

// Sends raw bytes to the listener and returns the status line's code, 0 if the connection closed with no answer.
static int _send_raw(int port, const string& bytes)
{
    r_socket s;
    s.connect("127.0.0.1", port);
    s.send(bytes.data(), bytes.size());
    string got;
    for(int i = 0; i < 100; ++i)
    {
        uint64_t ms = 100;
        if(!s.wait_till_recv_wont_block(ms))
            continue;
        char buf[512];
        int n = 0;
        try { n = s.recv(buf, sizeof(buf)); } catch(...) { break; }
        if(n <= 0)
            break;
        got.append(buf, (size_t)n);
        if(got.find("\r\n") != string::npos)
            break;
    }
    if(got.compare(0, 9, "HTTP/1.1 ") != 0)
        return 0;
    return atoi(got.c_str() + 9);
}

void test_r_onvif_push::test_push_head()
{
    auto ok = parse_push_head("POST /onvif/notify/abc123 HTTP/1.1\r\nHost: x\r\ncontent-length: 120\r\n\r\n");
    RTF_ASSERT_EQUAL(ok.status, 0);
    RTF_ASSERT_EQUAL(ok.id, string("abc123"));
    RTF_ASSERT_EQUAL(ok.content_length, (size_t)120);
    // A query is ignored; HTTP/1.0 is fine.
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/abc?x=1 HTTP/1.0\r\nContent-Length: 1\r\n\r\n").id, string("abc"));

    RTF_ASSERT_EQUAL(parse_push_head("GET /onvif/notify/abc HTTP/1.1\r\nContent-Length: 1\r\n\r\n").status, 405);
    RTF_ASSERT_EQUAL(parse_push_head("POST /other HTTP/1.1\r\nContent-Length: 1\r\n\r\n").status, 404);
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/ HTTP/1.1\r\nContent-Length: 1\r\n\r\n").status, 404);
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/../../x HTTP/1.1\r\nContent-Length: 1\r\n\r\n").status, 404);
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/" + string(65, 'a') + " HTTP/1.1\r\nContent-Length: 1\r\n\r\n").status, 404);
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/abc HTTP/1.1\r\n\r\n").status, 411);                         // no length
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/abc HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n").status, 411);
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/abc HTTP/1.1\r\nContent-Length: 65537\r\n\r\n").status, 413);  // one over the cap
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/abc HTTP/1.1\r\nContent-Length: 65536\r\n\r\n").status, 0);   // at it
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/abc HTTP/1.1\r\nContent-Length: 99999999999\r\n\r\n").status, 400);
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/abc HTTP/1.1\r\nContent-Length: -5\r\n\r\n").status, 400);
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/abc HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n").status, 400);
    RTF_ASSERT_EQUAL(parse_push_head("nonsense\r\n\r\n").status, 400);
    RTF_ASSERT_EQUAL(parse_push_head("POST /onvif/notify/abc FTP/1.1\r\n\r\n").status, 400);
    RTF_ASSERT_EQUAL(parse_push_head("").status, 400);
}

void test_r_onvif_push::test_ids_and_addresses()
{
    auto a = new_push_id();
    auto b = new_push_id();
    RTF_ASSERT_EQUAL(a.size(), (size_t)32);
    RTF_ASSERT(a != b);
    for(char c : a)
        RTF_ASSERT((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));

    RTF_ASSERT(is_ipv4_literal("192.168.1.5"));
    RTF_ASSERT(!is_ipv4_literal("cam.local"));
    RTF_ASSERT(!is_ipv4_literal(""));
    RTF_ASSERT(!is_ipv4_literal("192.168.1"));
    RTF_ASSERT(!is_ipv4_literal("192.168.1.5; rm"));

    RTF_ASSERT(local_ip_for("cam.local").empty());     // not an address
    RTF_ASSERT(local_ip_for("127.0.0.1").empty());     // loopback isn't a LAN address
}

void test_r_onvif_push::test_notify_parse()
{
    auto events = parse_notify(NOTIFY);
    RTF_ASSERT_EQUAL(events.size(), (size_t)1);
    RTF_ASSERT_EQUAL(events[0].topic, string("tns1:RuleEngine/CellMotionDetector/Motion"));
    RTF_ASSERT_EQUAL(events[0].property_operation, string("Changed"));
    RTF_ASSERT_EQUAL(events[0].utc_time, string("2026-11-10T20:48:50Z"));
    RTF_ASSERT_EQUAL(events[0].data.size(), (size_t)1);
    RTF_ASSERT_EQUAL(events[0].data[0].first, string("IsMotion"));
    RTF_ASSERT_EQUAL(events[0].data[0].second, string("true"));
    RTF_ASSERT(parse_notify("<a:Notify xmlns:a=\"urn:x\"/>").empty());
    RTF_ASSERT_THROWS(parse_notify("not xml <"), std::runtime_error);
    RTF_ASSERT_THROWS(parse_notify(""), std::runtime_error);
}

void test_r_onvif_push::test_notify_limits()
{
    // Too large.
    string big = string("<a>") + string(MAX_NOTIFY_BYTES, 'x') + "</a>";
    RTF_ASSERT_THROWS(parse_notify(big), std::runtime_error);

    // Too many messages: one over the cap throws, the cap itself is fine.
    auto message = []() {
        return string("<wsnt:NotificationMessage><wsnt:Topic>t</wsnt:Topic></wsnt:NotificationMessage>");
    };
    auto notify = [&](size_t n) {
        string s = "<s:Envelope xmlns:s=\"x\" xmlns:wsnt=\"y\"><s:Body><wsnt:Notify>";
        for(size_t i = 0; i < n; ++i)
            s += message();
        return s + "</wsnt:Notify></s:Body></s:Envelope>";
    };
    RTF_ASSERT_EQUAL(parse_notify(notify(MAX_NOTIFY_EVENTS)).size(), MAX_NOTIFY_EVENTS);
    RTF_ASSERT_THROWS(parse_notify(notify(MAX_NOTIFY_EVENTS + 1)), std::runtime_error);

    // A DTD with an entity is not expanded: the text stays what it was, and nothing blows up.
    string bomb = "<?xml version=\"1.0\"?><!DOCTYPE x [<!ENTITY a \"aaaaaaaaaa\"><!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;\">]>"
                  "<s:Envelope xmlns:s=\"x\" xmlns:wsnt=\"y\"><s:Body><wsnt:NotificationMessage><wsnt:Topic>&b;</wsnt:Topic></wsnt:NotificationMessage></s:Body></s:Envelope>";
    auto events = parse_notify(bomb);
    RTF_ASSERT(events.empty() || events[0].topic.find("aaaaaaaaaa") == string::npos);
}

void test_r_onvif_push::test_pull_point_support()
{
    auto caps = [](const string& v) {
        return "<s:Envelope xmlns:s=\"x\" xmlns:tt=\"y\"><s:Body><tds:GetCapabilitiesResponse xmlns:tds=\"z\"><tds:Capabilities>"
               "<tt:Events><tt:XAddr>http://1.2.3.4/onvif/events</tt:XAddr>" + v + "</tt:Events></tds:Capabilities></tds:GetCapabilitiesResponse></s:Body></s:Envelope>";
    };
    RTF_ASSERT_EQUAL(parse_ws_pull_point_support(caps("<tt:WSPullPointSupport>false</tt:WSPullPointSupport>")), 0);
    RTF_ASSERT_EQUAL(parse_ws_pull_point_support(caps("<tt:WSPullPointSupport>true</tt:WSPullPointSupport>")), 1);
    RTF_ASSERT_EQUAL(parse_ws_pull_point_support(caps("<tt:WSPullPointSupport> False </tt:WSPullPointSupport>")), 0);
    RTF_ASSERT_EQUAL(parse_ws_pull_point_support(caps("<tt:WSSubscriptionPolicySupport>true</tt:WSSubscriptionPolicySupport>")), -1);
    RTF_ASSERT_EQUAL(parse_ws_pull_point_support(caps("")), -1);
    RTF_ASSERT_EQUAL(parse_ws_pull_point_support("not xml <"), -1);
}

void test_r_onvif_push::test_listener_delivers()
{
    auto l = make_shared<r_onvif_push_listener>("127.0.0.1", 11060, 11079);
    mutex lock;
    vector<string> got;
    l->add("camid", "127.0.0.1", [&](const string& body) {
        lock_guard<mutex> g(lock);
        got.push_back(body);
    });
    RTF_ASSERT_EQUAL(l->url("camid"), "http://127.0.0.1:" + to_string(l->port()) + "/onvif/notify/camid");
    RTF_ASSERT_EQUAL(_send_raw(l->port(), _post("/onvif/notify/camid", NOTIFY)), 200);
    {
        lock_guard<mutex> g(lock);
        RTF_ASSERT_EQUAL(got.size(), (size_t)1);
        RTF_ASSERT_EQUAL(got[0], string(NOTIFY));
    }
    // The body can come in pieces, after the head.
    {
        r_socket s;
        s.connect("127.0.0.1", l->port());
        string whole = _post("/onvif/notify/camid", NOTIFY);
        size_t cut = whole.find("\r\n\r\n") + 4 + 10;
        s.send(whole.data(), cut);
        this_thread::sleep_for(chrono::milliseconds(100));
        s.send(whole.data() + cut, whole.size() - cut);
        char buf[64];
        uint64_t ms = 3000;
        RTF_ASSERT(s.wait_till_recv_wont_block(ms));
        RTF_ASSERT(s.recv(buf, sizeof(buf)) > 0);
        lock_guard<mutex> g(lock);
        RTF_ASSERT_EQUAL(got.size(), (size_t)2);
        RTF_ASSERT_EQUAL(got[1], string(NOTIFY));
    }
    // Once removed, the id is gone.
    l->remove("camid");
    RTF_ASSERT_EQUAL(_send_raw(l->port(), _post("/onvif/notify/camid", NOTIFY)), 0);   // no camera of this address is left: no answer at all
}

void test_r_onvif_push::test_listener_turns_away()
{
    auto l = make_shared<r_onvif_push_listener>("127.0.0.1", 11060, 11079);
    atomic<int> called {0};
    l->add("mine", "127.0.0.1", [&](const string&) { ++called; });
    l->add("other", "10.9.9.9", [&](const string&) { ++called; });

    RTF_ASSERT_EQUAL(_send_raw(l->port(), _post("/onvif/notify/nobody", NOTIFY)), 404);        // an id nobody has
    RTF_ASSERT_EQUAL(_send_raw(l->port(), _post("/onvif/notify/other", NOTIFY)), 403);         // another camera's id, from this address
    RTF_ASSERT_EQUAL(_send_raw(l->port(), _post("/elsewhere", NOTIFY)), 404);
    RTF_ASSERT_EQUAL(_send_raw(l->port(), "GET /onvif/notify/mine HTTP/1.1\r\n\r\n"), 405);
    RTF_ASSERT_EQUAL(_send_raw(l->port(), "POST /onvif/notify/mine HTTP/1.1\r\n\r\n"), 411);
    RTF_ASSERT_EQUAL(_send_raw(l->port(), "POST /onvif/notify/mine HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"), 411);
    RTF_ASSERT_EQUAL(_send_raw(l->port(), "POST /onvif/notify/mine HTTP/1.1\r\nContent-Length: 70000\r\n\r\n"), 413);
    // A head that never ends is cut at 8 KB.
    RTF_ASSERT_EQUAL(_send_raw(l->port(), "POST /onvif/notify/mine HTTP/1.1\r\nX: " + string(9000, 'a')), 413);
    // A body shorter than its Content-Length, then nothing, is not delivered (the connection ends).
    {
        r_socket s;
        s.connect("127.0.0.1", l->port());
        string partial = "POST /onvif/notify/mine HTTP/1.1\r\nContent-Length: 100\r\n\r\nshort";
        s.send(partial.data(), partial.size());
        s.close();
        this_thread::sleep_for(chrono::milliseconds(300));
    }
    RTF_ASSERT_EQUAL(called.load(), 0);
    RTF_ASSERT(l->rejected() >= 8);

    // And a caller that isn't any subscribed camera's address gets nothing, not even a refusal.
    r_onvif_push_listener strangers("127.0.0.1", 11080, 11089);
    strangers.add("x", "10.9.9.9", [&](const string&) { ++called; });
    RTF_ASSERT_EQUAL(_send_raw(strangers.port(), _post("/onvif/notify/x", NOTIFY)), 0);
    RTF_ASSERT_EQUAL(called.load(), 0);
}

void test_r_onvif_push::test_listener_shares_and_ports()
{
    auto a = r_onvif_push_listener::acquire("127.0.0.1", 11090, 11099);
    auto b = r_onvif_push_listener::acquire("127.0.0.1", 11090, 11099);
    RTF_ASSERT(a.get() == b.get());
    RTF_ASSERT_EQUAL(a->port(), 11090);

    // The next listener takes the next port in the range; a range with nothing free throws.
    r_onvif_push_listener second("127.0.0.1", 11090, 11099);
    RTF_ASSERT_EQUAL(second.port(), 11091);
    RTF_ASSERT_THROWS(r_onvif_push_listener("127.0.0.1", 11090, 11091), std::runtime_error);

    // Let go, and the port is free again.
    int port = a->port();
    a.reset();
    b.reset();
    auto c = r_onvif_push_listener::acquire("127.0.0.1", 11090, 11099);
    RTF_ASSERT_EQUAL(c->port(), port);
}

void test_r_onvif_push::test_subscribe_renew_unsubscribe()
{
    mutex lock;
    vector<pair<string, string>> calls;   // the SOAP action's operation and the request body
    auto envelope = [](const string& body) {
        return "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" "
               "xmlns:wsa=\"http://www.w3.org/2005/08/addressing\" xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\"><s:Body>" + body + "</s:Body></s:Envelope>";
    };
    r_web_server<r_socket> ws(11100, "127.0.0.1");
    auto answer = [&](const r_web_server<r_socket>&, r_socket&, const r_server_request& req) {
        const string body = req.get_body_as_string();
        string op = body.find("Subscribe") != string::npos && body.find("Unsubscribe") == string::npos ? "Subscribe" :
                    body.find("Unsubscribe") != string::npos ? "Unsubscribe" : body.find("Renew") != string::npos ? "Renew" : "other";
        {
            lock_guard<mutex> g(lock);
            calls.push_back({op, body});
        }
        r_server_response response(response_ok);
        response.set_content_type("application/soap+xml");
        if(op == "Subscribe")
            response.set_body(envelope("<wsnt:SubscribeResponse><wsnt:SubscriptionReference><wsa:Address>http://127.0.0.1:11100/onvif/sub1</wsa:Address>"
                                       "</wsnt:SubscriptionReference><wsnt:TerminationTime>2026-11-10T20:50:50Z</wsnt:TerminationTime></wsnt:SubscribeResponse>"));
        else
            response.set_body(envelope("<wsnt:" + op + "Response/>"));
        return response;
    };
    ws.add_route(METHOD_POST, "/onvif/events", answer);
    ws.add_route(METHOD_POST, "/onvif/sub1", answer);
    ws.start();
    for(int i = 0; i < 200; ++i)
    {
        try { r_socket probe; probe.connect("127.0.0.1", 11100); probe.close(); break; }
        catch(...) { this_thread::sleep_for(chrono::milliseconds(10)); }
    }

    r_onvif_cam cam("127.0.0.1", 11100, "http", "/onvif/device_service", r_nullable<string>(), r_nullable<string>());
    auto sub = cam.subscribe_push("http://127.0.0.1:11100/onvif/events", "http://192.168.1.5:11040/onvif/notify/abc", 120);
    RTF_ASSERT_EQUAL(sub.address, string("http://127.0.0.1:11100/onvif/sub1"));
    cam.renew(sub, 120);
    cam.unsubscribe(sub);
    ws.stop();

    lock_guard<mutex> g(lock);
    RTF_ASSERT_EQUAL(calls.size(), (size_t)3);
    RTF_ASSERT_EQUAL(calls[0].first, string("Subscribe"));
    RTF_ASSERT(calls[0].second.find("<wsa:Address>http://192.168.1.5:11040/onvif/notify/abc</wsa:Address>") != string::npos);
    RTF_ASSERT(calls[0].second.find("ConsumerReference") != string::npos);
    RTF_ASSERT(calls[0].second.find("<wsnt:InitialTerminationTime>PT120S</wsnt:InitialTerminationTime>") != string::npos);
    RTF_ASSERT_EQUAL(calls[1].first, string("Renew"));
    RTF_ASSERT(calls[1].second.find("PT120S") != string::npos);
    RTF_ASSERT_EQUAL(calls[2].first, string("Unsubscribe"));
}
