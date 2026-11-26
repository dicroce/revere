#include "test_r_plugin_action_request.h"
#include "r_vss/r_plugin_action_request.h"
#include "r_http/r_http_exception.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// POST /system_plugin_action driven with real HTTP requests read from a socket, with the plugins behind it faked.

using namespace r_vss;
using namespace r_http;
using namespace r_utils;
using namespace std;
using json = nlohmann::json;

REGISTER_TEST_FIXTURE(test_r_plugin_action_request);

static const string TOKEN = string(64, 'b');

namespace
{

class fake_socket : public r_socket_base
{
public:
    explicit fake_socket(const string& bytes) : _bytes(bytes) {}

    void connect(const string&, int) override {}
    void close() override { _closed = true; }
    bool valid() const override { return !_closed; }
    int send(const void*, size_t len) override { return (int)len; }
    int recv(void* buf, size_t len) override
    {
        size_t n = min(len, _bytes.size() - _pos);
        memcpy(buf, _bytes.data() + _pos, n);
        _pos += n;
        return (int)n;
    }
    bool wait_till_recv_wont_block(uint64_t&) const override { return true; }
    bool wait_till_send_wont_block(uint64_t&) const override { return true; }
    bool peer_closed() const override { return _pos >= _bytes.size(); }
    string get_peer_ip() const override { return "127.0.0.1"; }

private:
    string _bytes;
    size_t _pos {0};
    bool _closed {false};
};

// "Front" offers a plain action and a choice; "Quiet" offers nothing; "Mute" offers the same lines but has no action export.
const char* FRONT_MESSAGE = R"({"lines": [
    {"text": "Front: in Apple Home", "action": "reset:front", "action_label": "Reset pairing"},
    {"text": "Porch: Live", "choice": {"value": "live", "action": "mode:porch:", "options": [
        {"value": "off", "name": "Off"}, {"value": "live", "name": "Live"}, {"value": "apple_alerts", "name": "Live + Apple alerts"}]}},
    {"text": "no action here"}]})";

struct box
{
    r_ws_auth auth;
    vector<pair<string, string>> ran;
    plugin_action_host host;

    box()
    {
        auth.set_local_token(TOKEN);
        host.loaded = [] { return vector<string>{"front", "quiet", "mute"}; };
        host.status_message = [](const string& name) { return name == "front" || name == "mute" ? string(FRONT_MESSAGE) : string(R"({"lines": [{"text": "hi"}]})"); };
        host.run = [this](const string& name, const string& action) { if(name == "mute") return false; ran.push_back({name, action}); return true; };
    }
};

// The HTTP status the route answers, or the status of what it throws, with the body it sent.
pair<int, json> _post(box& b, const string& body, const string& token = TOKEN)
{
    string raw = "POST /system_plugin_action HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\n";
    if(!token.empty())
        raw += "Authorization: Bearer " + token + "\r\n";
    raw += "Content-Length: " + to_string(body.size()) + "\r\n\r\n" + body;

    fake_socket sok(raw);
    r_server_request request;
    request.read_request(sok);
    try
    {
        auto response = post_system_plugin_action(request, b.auth, b.host);
        return {(int)response.get_status_code(), json::parse(response.get_body_as_string(), nullptr, false)};
    }
    catch(const r_http_exception& ex)
    {
        return {ex.get_status(), json()};
    }
}

string _body(const string& name, const string& action)
{
    return json{{"name", name}, {"action", action}}.dump();
}

}

void test_r_plugin_action_request::test_offered_actions()
{
    auto offered = plugin_offered_actions(FRONT_MESSAGE);
    RTF_ASSERT((offered == vector<string>{"reset:front", "mode:porch:off", "mode:porch:live", "mode:porch:apple_alerts"}));

    // Nothing offered, or not a status message at all.
    RTF_ASSERT(plugin_offered_actions(R"({"lines": [{"text": "hi"}]})").empty());
    RTF_ASSERT(plugin_offered_actions("").empty());
    RTF_ASSERT(plugin_offered_actions("nonsense").empty());
    // A choice with no action offers nothing (the desktop draws no radio list for it), and an option with no value is skipped.
    RTF_ASSERT(plugin_offered_actions(R"({"lines": [{"text": "t", "choice": {"options": [{"value": "a", "name": "A"}]}}]})").empty());
    RTF_ASSERT((plugin_offered_actions(R"({"lines": [{"text": "t", "choice": {"action": "m:", "options": [{"name": "A"}, {"value": "b", "name": "B"}]}}]})") == vector<string>{"m:b"}));
}

void test_r_plugin_action_request::test_needs_a_token()
{
    box b;
    // No token, a wrong one, and one that isn't the local token or a session: 401, and nothing runs.
    RTF_ASSERT(_post(b, _body("front", "reset:front"), "").first == 401);
    RTF_ASSERT(_post(b, _body("front", "reset:front"), string(64, 'c')).first == 401);
    RTF_ASSERT(b.ran.empty());
    // The check comes before anything else: a bad body without a token is 401 too, not 400.
    RTF_ASSERT(_post(b, "not json", "").first == 401);
    // A session token works as the local one does.
    auto session = b.auth.issue_session();
    RTF_ASSERT(_post(b, _body("front", "reset:front"), session).first == 200);
    RTF_ASSERT(b.ran.size() == 1);
}

void test_r_plugin_action_request::test_runs_an_offered_action()
{
    box b;
    auto r = _post(b, _body("front", "reset:front"));
    RTF_ASSERT(r.first == 200);
    RTF_ASSERT(r.second["name"] == "front" && r.second["action"] == "reset:front");
    RTF_ASSERT((b.ran == vector<pair<string, string>>{{"front", "reset:front"}}));
}

void test_r_plugin_action_request::test_runs_a_choice_pick()
{
    box b;
    // The choice's action and the picked option's value, as the desktop's panel runs them.
    RTF_ASSERT(_post(b, _body("front", "mode:porch:apple_alerts")).first == 200);
    RTF_ASSERT((b.ran == vector<pair<string, string>>{{"front", "mode:porch:apple_alerts"}}));
}

void test_r_plugin_action_request::test_refuses_what_is_not_offered()
{
    box b;
    // A made-up action, a choice's prefix with no value, a value no option has, and an empty action: 400, never run.
    for(auto action : {"reset:other", "mode:porch:", "mode:porch:recording", "mode:kitchen:live", ""})
    {
        auto r = _post(b, _body("front", action));
        RTF_ASSERT(r.first == 400);
        RTF_ASSERT(r.second.contains("error"));
    }
    // A plugin that offers nothing.
    RTF_ASSERT(_post(b, _body("quiet", "reset:front")).first == 400);
    // An action offered by another plugin isn't this one's.
    RTF_ASSERT(_post(b, _body("quiet", "mode:porch:live")).first == 400);
    RTF_ASSERT(b.ran.empty());
}

void test_r_plugin_action_request::test_refuses_bad_requests()
{
    box b;
    RTF_ASSERT(_post(b, _body("nothing", "reset:front")).first == 404);          // not loaded
    RTF_ASSERT(_post(b, "not json").first == 400);
    RTF_ASSERT(_post(b, R"({"name": "front"})").first == 400);                    // no action
    RTF_ASSERT(_post(b, R"({"action": "reset:front"})").first == 400);            // no name
    RTF_ASSERT(_post(b, R"({"name": 3, "action": "reset:front"})").first == 400); // not strings
    RTF_ASSERT(_post(b, R"({"name": "front", "action": ["reset:front"]})").first == 400);
    // A plugin that offers the action but has no action export to run it.
    auto mute = _post(b, _body("mute", "reset:front"));
    RTF_ASSERT(mute.first == 400 && mute.second.contains("error"));
    RTF_ASSERT(b.ran.empty());
}
