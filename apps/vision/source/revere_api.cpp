
#include "revere_api.h"
#include "utils.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_local_api_token.h"
#include "r_utils/r_logger.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_http/r_methods.h"
#include <map>
#include <mutex>

using namespace vision;
using namespace r_utils;
using namespace r_http;
using namespace std;
using json = nlohmann::json;

namespace
{
const int REVERE_HTTP_PORT = 8088;
const int REVERE_HTTPS_PORT = 8443;

const char* TRUSTED_FILE = "vision_trusted_servers.json";   // host -> fingerprint (not secret)
const char* SESSIONS_FILE = "vision_sessions.json";         // host -> session token (owner-only)

// Guards the two files (read-modify-write), the pending problem and the
// RTSP credential cache.
mutex _lock;
r_nullable<identity_problem> _pending;
map<string, rtsp_credentials> _rtsp_cache;   // keyed by _key(host)

string _dir()
{
    try { return top_dir(); } catch(...) { return string(); }
}

string _key(const string& host)
{
    // One entry per host as typed, case-insensitively.
    return r_string_utils::to_lower(r_string_utils::strip(host));
}

json _load(const char* name)
{
    auto dir = _dir();
    if(dir.empty())
        return json::object();
    auto path = dir + PATH_SLASH + name;
    try
    {
        if(!r_fs::file_exists(path))
            return json::object();
        auto bytes = r_fs::read_file(path);
        auto j = json::parse(string(bytes.begin(), bytes.end()));
        return j.is_object() ? j : json::object();
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("vision: ignoring unreadable %s: %s", path.c_str(), ex.what());
        return json::object();
    }
}

void _save(const char* name, const json& j, bool secret)
{
    auto dir = _dir();
    if(dir.empty())
        R_THROW(("vision: no data directory to save %s in.", name));
    auto path = dir + PATH_SLASH + name;
    auto text = j.dump(2);
    if(secret)
        r_fs::atomic_write_private_file((const uint8_t*)text.data(), text.size(), path);
    else
        r_fs::atomic_write_file((const uint8_t*)text.data(), text.size(), path);
}

string _get(const char* name, const string& host)
{
    lock_guard<mutex> g(_lock);
    auto j = _load(name);
    auto it = j.find(_key(host));
    return (it != j.end() && it->is_string()) ? it->get<string>() : string();
}

void _set(const char* name, const string& host, const string& value, bool secret)
{
    lock_guard<mutex> g(_lock);
    auto j = _load(name);
    if(value.empty())
        j.erase(_key(host));
    else
        j[_key(host)] = value;
    _save(name, j, secret);
}
}

bool vision::is_remote(const string& host)
{
    return !r_local_api_token::is_loopback_host(host);
}

int vision::revere_port(const string& host)
{
    return is_remote(host) ? REVERE_HTTPS_PORT : REVERE_HTTP_PORT;
}

unique_ptr<r_socket_base> vision::revere_connect(const string& host, uint64_t io_timeout_ms)
{
    if(!is_remote(host))
    {
        auto sok = make_unique<r_socket>();
        sok->set_io_timeout(io_timeout_ms);
        sok->connect(host, REVERE_HTTP_PORT);
        return sok;
    }

    // Encrypted; the server is authenticated by its pinned fingerprint rather
    // than a CA (its certificate is self-signed).
    auto sok = make_unique<r_ssl_socket>(false);
    sok->set_io_timeout(io_timeout_ms);
    sok->connect(host, REVERE_HTTPS_PORT);

    auto presented = sok->peer_cert_fingerprint_sha256();
    auto pinned = _get(TRUSTED_FILE, host);
    if(presented.empty() || presented != pinned)
    {
        sok->close();
        {
            lock_guard<mutex> g(_lock);
            identity_problem p;
            p.host = host;
            p.presented = presented;
            p.pinned = pinned;
            _pending.set_value(p);
        }
        if(pinned.empty())
            throw untrusted_server("Revere at " + host + " is not trusted yet: confirm its certificate fingerprint.");
        throw untrusted_server("Revere at " + host + " presented a different certificate than before.");
    }

    return sok;
}

void vision::authorize(r_client_request& req, const string& host)
{
    // Read every time: tiny files, and Revere (or another vision process)
    // may replace them.
    if(!is_remote(host))
    {
        auto local = r_local_api_token::read(_dir());
        if(!local.is_null())
        {
            req.add_header("Authorization", r_local_api_token::bearer(local.value()));
            return;
        }
    }

    auto session = _get(SESSIONS_FILE, host);
    if(!session.empty())
        req.add_header("Authorization", r_local_api_token::bearer(session));
}

void vision::note_status(const string& host, int status)
{
    if(status == 401 && is_remote(host))
    {
        try { _set(SESSIONS_FILE, host, string(), true); }
        catch(const exception& ex) { R_LOG_WARNING("vision: could not drop session for %s: %s", host.c_str(), ex.what()); }
    }
}

r_nullable<identity_problem> vision::pending_identity_problem()
{
    lock_guard<mutex> g(_lock);
    return _pending;
}

void vision::trust_server(const string& host, const string& fingerprint)
{
    _set(TRUSTED_FILE, host, fingerprint, false);
    // A new identity starts with no session: the old one was the old server's.
    _set(SESSIONS_FILE, host, string(), true);
    lock_guard<mutex> g(_lock);
    if(!_pending.is_null() && _key(_pending.value().host) == _key(host))
        _pending.clear();
    R_LOG_INFO("vision: trusting Revere at %s, fingerprint %s", host.c_str(), fingerprint.c_str());
}

void vision::dismiss_identity_problem()
{
    lock_guard<mutex> g(_lock);
    _pending.clear();
}

r_nullable<string> vision::trusted_fingerprint(const string& host)
{
    r_nullable<string> out;
    auto fp = _get(TRUSTED_FILE, host);
    if(!fp.empty())
        out.set_value(fp);
    return out;
}

void vision::forget_server(const string& host)
{
    _set(TRUSTED_FILE, host, string(), false);
    _set(SESSIONS_FILE, host, string(), true);
    lock_guard<mutex> g(_lock);
    _rtsp_cache.erase(_key(host));
    if(!_pending.is_null() && _key(_pending.value().host) == _key(host))
        _pending.clear();
}

bool vision::session_needed(const string& host)
{
    return is_remote(host) && _get(SESSIONS_FILE, host).empty();
}

void vision::login(const string& host, const string& password)
{
    auto sok = revere_connect(host, 15000);   // pin checked before the password goes out

    r_client_request req(host, revere_port(host));
    req.set_method(METHOD_POST);
    req.set_uri("/login");
    req.set_content_type("application/json");
    req.set_body(json{{"password", password}}.dump());
    req.write_request(*sok);

    r_client_response resp;
    resp.read_response(*sok, 15000);
    sok->close();

    auto status = resp.get_status();
    if(status == 401)
        throw runtime_error("Wrong password. (After several wrong attempts Revere refuses this computer for 30 seconds.)");
    if(status == 404)
        throw runtime_error("This Revere doesn't support signing in; update it.");
    if(status != 200)
        throw runtime_error("Sign-in failed (HTTP " + to_string(status) + ").");

    auto body = resp.get_body_as_string();
    string token;
    try
    {
        if(!body.is_null())
            token = json::parse(body.value()).value("token", string());
    }
    catch(...) {}
    if(token.empty())
        throw runtime_error("Sign-in failed: Revere's reply had no session token.");

    _set(SESSIONS_FILE, host, token, true);
    R_LOG_INFO("vision: signed in to Revere at %s", host.c_str());
}

r_nullable<rtsp_credentials> vision::get_rtsp_credentials(const string& host)
{
    r_nullable<rtsp_credentials> out;
    if(host.empty())
        return out;

    {
        lock_guard<mutex> g(_lock);
        auto it = _rtsp_cache.find(_key(host));
        if(it != _rtsp_cache.end())
        {
            out.set_value(it->second);
            return out;
        }
    }

    try
    {
        auto sok = revere_connect(host, 5000);
        r_client_request req(host, revere_port(host));
        req.set_uri("/rtsp_credentials");
        authorize(req, host);
        req.write_request(*sok);
        r_client_response resp;
        resp.read_response(*sok, 5000);
        sok->close();
        note_status(host, resp.get_status());
        if(resp.get_status() != 200)
        {
            R_LOG_WARNING("vision: no RTSP credential from %s (HTTP %d)", host.c_str(), resp.get_status());
            return out;
        }
        auto body = resp.get_body_as_string();
        if(body.is_null())
            return out;
        auto j = json::parse(body.value());
        rtsp_credentials c;
        c.username = j.value("username", string());
        c.password = j.value("password", string());
        if(c.username.empty() || c.password.empty())
            return out;
        {
            lock_guard<mutex> g(_lock);
            _rtsp_cache[_key(host)] = c;
        }
        out.set_value(c);
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("vision: couldn't get the RTSP credential from %s: %s", host.c_str(), ex.what());
    }
    return out;
}

string vision::host_of_url(const string& url)
{
    const string scheme = "rtsp://";
    if(url.compare(0, scheme.size(), scheme) != 0)
        return string();
    auto rest = url.substr(scheme.size());
    auto at = rest.find('@');
    auto slash = rest.find('/');
    if(at != string::npos && (slash == string::npos || at < slash))
        rest = rest.substr(at + 1);
    if(!rest.empty() && rest.front() == '[')
    {
        auto close = rest.find(']');
        return close == string::npos ? string() : rest.substr(1, close - 1);
    }
    auto end = rest.find_first_of(":/");
    return end == string::npos ? rest : rest.substr(0, end);
}
