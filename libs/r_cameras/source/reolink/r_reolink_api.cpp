#include "r_cameras/reolink/r_reolink_api.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_http/r_methods.h"
#include "r_utils/r_socket.h"
#include "r_utils/3rdparty/json/json.h"
#include <cctype>
#include <cstdio>
#include <stdexcept>

using namespace r_cameras;
using namespace r_utils;
using namespace std;
using json = nlohmann::json;

static const uint64_t TIMEOUT_MS = 4000;

// The camera's error for a failed command, for messages.
static string _error_of(const json& cmd)
{
    if (cmd.contains("error") && cmd["error"].is_object())
    {
        auto& e = cmd["error"];
        return e.value("detail", string()) + " (rspCode " + to_string(e.value("rspCode", 0)) + ")";
    }
    return "code " + to_string(cmd.value("code", -1));
}

// The response's one command, checked.
static json _command(const string& text, const string& name)
{
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_array() || j.empty())
        throw runtime_error("Reolink " + name + ": unexpected response");
    auto& cmd = j[0];
    if (cmd.value("code", -1) != 0)
        throw runtime_error("Reolink " + name + " failed: " + _error_of(cmd));
    if (!cmd.contains("value") || !cmd["value"].is_object())
        throw runtime_error("Reolink " + name + ": no value in the response");
    return cmd["value"];
}

reolink_state r_cameras::reolink_parse_state(const string& md_json, const string& ai_json)
{
    reolink_state state;
    state.motion = _command(md_json, "GetMdState").value("state", 0) != 0;

    auto ai = _command(ai_json, "GetAiState");
    for (auto it = ai.begin(); it != ai.end(); ++it)
    {
        if (!it.value().is_object())
            continue;   // "channel"
        if (it.value().value("support", 0) == 0)
            continue;
        state.ai[it.key()] = it.value().value("alarm_state", 0) != 0;
    }
    return state;
}

static string _pct(const string& s)
{
    string out;
    char buf[4];
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else
        {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

r_reolink_client::r_reolink_client(const string& host, const r_nullable<string>& username, const r_nullable<string>& password, int port) :
    _host(host),
    _port(port),
    _username(username.is_null() ? string() : username.value()),
    _password(password.is_null() ? string() : password.value())
{
}

string r_reolink_client::_get(const string& cmd)
{
    r_socket sok;
    sok.set_io_timeout(TIMEOUT_MS);
    sok.connect(_host, _port);

    r_http::r_client_request request(_host, _port);
    request.set_method(r_http::METHOD_GET);
    request.set_uri("/cgi-bin/api.cgi?cmd=" + cmd + "&channel=0&user=" + _pct(_username) + "&password=" + _pct(_password));
    request.add_header("Connection", "close");
    request.write_request(sok, TIMEOUT_MS);

    r_http::r_client_response response;
    response.read_response(sok, TIMEOUT_MS);
    if (response.get_status() != 200)
        throw runtime_error("Reolink " + cmd + ": HTTP " + to_string(response.get_status()));
    auto b = response.get_body_as_string();
    return b.is_null() ? string() : b.value();
}

reolink_state r_reolink_client::poll()
{
    auto md = _get("GetMdState");
    auto ai = _get("GetAiState");
    return reolink_parse_state(md, ai);
}
