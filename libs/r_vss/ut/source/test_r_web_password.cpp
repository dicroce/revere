#include "test_r_web_password.h"
#include "r_vss/r_web_password.h"
#include "r_vss/r_ws_auth.h"
#include "r_vss/r_appliance.h"
#include "r_http/r_http_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

// The /auth_status and /set_password handlers r_ws registers, driven with real
// HTTP requests read from a socket whose peer address the test picks: so the
// appliance flag, the local token and the caller's address all go through the
// same code a request to Revere does.

using namespace r_vss;
using namespace r_http;
using namespace r_utils;
using namespace std;
namespace fs = std::filesystem;
using json = nlohmann::json;

REGISTER_TEST_FIXTURE(test_r_web_password);

static const string LOOPBACK = "127.0.0.1";
static const string LAN = "192.168.1.20";
static const string LOCAL_TOKEN = string(64, 'a');

static string s_top;

namespace
{

// Replays one request's bytes; reports the peer address it was given.
class fake_socket : public r_socket_base
{
public:
    fake_socket(const string& peer, const string& bytes) : _peer(peer), _bytes(bytes) {}

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
    string get_peer_ip() const override { return _peer; }

private:
    string _peer;
    string _bytes;
    size_t _pos {0};
    bool _closed {false};
};

struct box
{
    r_ws_auth auth;
    unique_ptr<r_web_password> password;

    box()
    {
        auth.set_local_token(LOCAL_TOKEN);
        password = make_unique<r_web_password>(s_top, auth);
        vector<uint8_t> key(32);
        random_device rd;
        for(auto& b : key)
            b = (uint8_t)rd();
        password->set_master_key(key);
    }
};

void _make_appliance()
{
    string contents = R"({"appliance": true, "claim_secret": "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "label": "ut", "allow_exec": false})";
    r_fs::write_file((const uint8_t*)contents.data(), contents.size(), r_appliance::path(s_top));
}

json _auth_status(box& b, const string& peer)
{
    fake_socket sok(peer, "GET /auth_status HTTP/1.1\r\nHost: localhost\r\n\r\n");
    r_server_request request;
    request.read_request(sok);
    auto response = b.password->get_auth_status(sok, request);
    return json::parse(response.get_body_as_string());
}

// The HTTP status /set_password answers: 200, or the status of what it throws.
int _set_password(box& b, const string& peer, const string& new_password, const string& token = string(), const string& current = string())
{
    json body;
    body["new_password"] = new_password;
    if(!current.empty())
        body["current_password"] = current;
    auto text = body.dump();

    string raw = "POST /set_password HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\n";
    if(!token.empty())
        raw += "Authorization: Bearer " + token + "\r\n";
    raw += "Content-Length: " + to_string(text.size()) + "\r\n\r\n" + text;

    fake_socket sok(peer, raw);
    r_server_request request;
    request.read_request(sok);
    try
    {
        auto response = b.password->post_set_password(sok, request);
        return (int)response.get_status_code();
    }
    catch(const r_http_exception& ex)
    {
        return ex.get_status();
    }
}

bool _is(box& b, const string& password)
{
    auto stored = b.password->load();
    return !stored.is_null() && stored.value() == password;
}

}

void test_r_web_password::setup()
{
    s_top = (fs::temp_directory_path() / ("r_web_password_ut_" + to_string(random_device()()))).string();
    fs::create_directories(s_top);
}

void test_r_web_password::teardown()
{
    error_code ec;
    fs::remove_all(s_top, ec);
}

void test_r_web_password::test_auth_status_normal_install()
{
    box b;
    RTF_ASSERT(!b.password->appliance());

    auto j = _auth_status(b, LOOPBACK);
    RTF_ASSERT(j["password_set"] == false);
    RTF_ASSERT(j["appliance"] == false);
    RTF_ASSERT(j["local"] == true);

    j = _auth_status(b, LAN);
    RTF_ASSERT(j["local"] == false);
    j = _auth_status(b, "::1");
    RTF_ASSERT(j["local"] == true);

    b.password->store("pw");
    RTF_ASSERT(_auth_status(b, LAN)["password_set"] == true);
}

void test_r_web_password::test_auth_status_appliance()
{
    _make_appliance();
    box b;
    RTF_ASSERT(b.password->appliance());

    auto j = _auth_status(b, LAN);
    RTF_ASSERT(j["appliance"] == true);
    RTF_ASSERT(j["password_set"] == false);
    RTF_ASSERT(j["local"] == false);
    RTF_ASSERT(_auth_status(b, LOOPBACK)["local"] == true);
    // The claim secret stays out of it.
    RTF_ASSERT(j.dump().find("ABCDEFGHIJ") == string::npos);
}

void test_r_web_password::test_normal_bootstrap_from_loopback_only()
{
    box b;
    RTF_ASSERT(_set_password(b, LAN, "first") == 403);
    RTF_ASSERT(!b.password->password_set());

    // From this machine, no credentials needed (unchanged).
    RTF_ASSERT(_set_password(b, LOOPBACK, "first") == 200);
    RTF_ASSERT(_is(b, "first"));
}

void test_r_web_password::test_normal_change()
{
    box b;
    b.password->store("old");

    RTF_ASSERT(_set_password(b, LAN, "new") == 401);
    RTF_ASSERT(_set_password(b, LAN, "new", string(), "wrong") == 401);
    RTF_ASSERT(_is(b, "old"));

    RTF_ASSERT(_set_password(b, LAN, "new", string(), "old") == 200);
    RTF_ASSERT(_is(b, "new"));

    auto session = b.auth.issue_session();
    RTF_ASSERT(_set_password(b, LAN, "newer", session) == 200);
    RTF_ASSERT(_is(b, "newer"));
    // A change ends every session.
    RTF_ASSERT(!b.auth.valid(session));

    RTF_ASSERT(_set_password(b, LOOPBACK, "newest", LOCAL_TOKEN) == 200);
    RTF_ASSERT(_is(b, "newest"));
}

void test_r_web_password::test_appliance_bootstrap_needs_local_token()
{
    _make_appliance();
    box b;

    // Loopback alone isn't enough on an appliance: no other local process can
    // claim the box before its agent does.
    RTF_ASSERT(_set_password(b, LOOPBACK, "first") == 403);
    RTF_ASSERT(_set_password(b, LOOPBACK, "first", b.auth.issue_session()) == 403);
    RTF_ASSERT(_set_password(b, LOOPBACK, "first", "not-a-token") == 403);
    // Nor is the local token from another machine.
    RTF_ASSERT(_set_password(b, LAN, "first", LOCAL_TOKEN) == 403);
    RTF_ASSERT(!b.password->password_set());

    // The agent: loopback with the local token.
    RTF_ASSERT(_set_password(b, LOOPBACK, "first", LOCAL_TOKEN) == 200);
    RTF_ASSERT(_is(b, "first"));
}

void test_r_web_password::test_appliance_change_needs_local_token()
{
    _make_appliance();
    box b;
    b.password->store("cloud-1");

    // The current password, a session or nothing: 403, from anywhere.
    for(const auto& peer : {LAN, LOOPBACK})
    {
        RTF_ASSERT(_set_password(b, peer, "mine", string(), "cloud-1") == 403);
        RTF_ASSERT(_set_password(b, peer, "mine", b.auth.issue_session()) == 403);
        RTF_ASSERT(_set_password(b, peer, "mine") == 403);
    }
    RTF_ASSERT(_is(b, "cloud-1"));

    // The local token (the agent applying a rotated password).
    RTF_ASSERT(_set_password(b, LOOPBACK, "cloud-2", LOCAL_TOKEN) == 200);
    RTF_ASSERT(_is(b, "cloud-2"));
}
