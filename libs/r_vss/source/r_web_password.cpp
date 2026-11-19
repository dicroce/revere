#include "r_vss/r_web_password.h"
#include "r_vss/r_appliance.h"
#include "r_http/r_http_exception.h"
#include "r_http/r_methods.h"
#include "r_utils/r_credential_crypto.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/r_local_api_token.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_ssl_server_socket.h"
#include "r_utils/3rdparty/json/json.h"

using namespace r_vss;
using namespace r_utils;
using namespace r_http;
using namespace std;
using json = nlohmann::json;

r_web_password::r_web_password(const string& top_dir, r_ws_auth& auth) :
    _top_dir(top_dir),
    _auth(auth),
    _appliance(r_appliance::read_flag(top_dir))
{
}

void r_web_password::set_master_key(const vector<uint8_t>& key)
{
    _master_key = key;
}

bool r_web_password::appliance() const
{
    return _appliance;
}

string r_web_password::_path() const
{
    return _top_dir + PATH_SLASH + "system_password";
}

bool r_web_password::password_set() const
{
    return r_fs::file_exists(_path());
}

r_nullable<string> r_web_password::load()
{
    r_nullable<string> out;
    if(_master_key.empty() || !r_fs::file_exists(_path()))
        return out;
    auto bytes = r_fs::read_file(_path());
    string enc(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    out.set_value(r_credential_crypto::decrypt_credential(enc, _master_key));
    return out;
}

void r_web_password::store(const string& password)
{
    if(_master_key.empty())
        R_THROW(("Secure storage unavailable; cannot set the local web password."));
    auto enc = r_credential_crypto::encrypt_credential(password, _master_key);
    // Write to a temp file then atomically rename, so a crash mid-write can't
    // leave a truncated (undecryptable) password file.
    auto tmp = _path() + ".tmp";
    r_fs::write_file(reinterpret_cast<const uint8_t*>(enc.data()), enc.size(), tmp);
    r_fs::atomic_rename_file(tmp, _path());
}

void r_web_password::clear()
{
    auto path = _path();
    if(r_fs::file_exists(path))
        r_fs::remove_file(path);
}

r_nullable<string> r_web_password::request_token(const r_server_request& request)
{
    auto method = request.get_method();
    bool safe = (method == METHOD_GET || method == METHOD_HEAD);
    return r_ws_auth::extract_token(request.get_header("authorization"), request.get_header("cookie"), safe);
}

string r_web_password::peer_ip(r_socket_base& conn)
{
    try
    {
        return conn.get_peer_ip();
    }
    catch(...)
    {
        return string();
    }
}

bool r_web_password::is_tls(r_socket_base& conn)
{
    return dynamic_cast<r_ssl_server_socket*>(&conn) != nullptr;
}

r_server_response r_web_password::get_auth_status(r_socket_base& conn, const r_server_request&)
{
    // Unauthenticated on purpose: lets the web UI decide between a first-run
    // "create password" screen, an appliance's "not set up yet" page and a
    // "login" screen. Only leaks whether a system password has been
    // configured, whether this is an appliance, and whether the caller is on
    // this machine (which it knows already).
    auto j = r_appliance::auth_status(password_set(), _appliance, r_local_api_token::is_loopback_host(peer_ip(conn)));

    r_server_response response;
    response.set_content_type("text/json");
    response.set_body(j.dump());
    return response;
}

r_server_response r_web_password::post_set_password(r_socket_base& conn, const r_server_request& request)
{
    auto address = peer_ip(conn);
    try
    {
        auto req = json::parse(request.get_body_as_string());
        if(!req.contains("new_password") || !req["new_password"].is_string())
            R_STHROW(r_http_400_exception, ("Missing new_password."));
        auto new_password = req["new_password"].get<string>();
        if(new_password.empty())
            R_STHROW(r_http_400_exception, ("Password must not be empty."));

        // How the caller authenticated, for r_appliance::check_set_password.
        // The local token is told apart from a session: on an appliance only
        // it may set the password. The current-password path is for normal
        // installs only, and counts toward the same per-address lockout as
        // /login, so it can't be used to guess around it.
        bool password_set_now = password_set();
        auto auth = r_appliance::set_password_auth::none;
        auto token = request_token(request);
        auto local = _auth.local_token();
        if(!token.is_null() && !local.empty() && r_ws_auth::constant_time_equal(token.value(), local))
            auth = r_appliance::set_password_auth::local_token;
        else if(!token.is_null() && _auth.valid(token.value()))
            auth = r_appliance::set_password_auth::session_token;
        else if(password_set_now && !_appliance)
        {
            if(_auth.locked_out(address))
                R_STHROW(r_http_401_exception, ("Too many failed attempts; try again later."));
            if(req.contains("current_password") && req["current_password"].is_string())
            {
                auto stored = load();
                bool ok = !stored.is_null() && r_ws_auth::constant_time_equal(req["current_password"].get<string>(), stored.value());
                if(ok)
                {
                    _auth.record_success(address);
                    auth = r_appliance::set_password_auth::current_password;
                }
                else _auth.record_failure(address);
            }
        }

        switch(r_appliance::check_set_password(_appliance, password_set_now, r_local_api_token::is_loopback_host(address), auth))
        {
            case r_appliance::set_password_verdict::allow:
                break;
            case r_appliance::set_password_verdict::loopback_only:
                R_STHROW(r_http_403_exception, ("The first password must be set on the Revere machine itself."));
            case r_appliance::set_password_verdict::unauthorized:
                R_STHROW(r_http_401_exception, ("Provide a valid token or current_password to change the local web password."));
            case r_appliance::set_password_verdict::cloud_managed:
                R_STHROW(r_http_403_exception, ("This Revere box's local password is managed in the Revere app."));
        }

        store(new_password);

        // Every outstanding session must sign in again with the new password.
        _auth.revoke_all_sessions();

        json j;
        j["status"] = "ok";

        r_server_response response;
        response.set_content_type("text/json");
        response.set_body(j.dump());
        // The caller's own cookie (if any) died with its session.
        response.add_additional_header("Set-Cookie", r_ws_auth::clear_session_cookie(is_tls(conn)));
        return response;
    }
    catch(const r_http_400_exception&) { throw; }
    catch(const r_http_401_exception&) { throw; }
    catch(const r_http_403_exception&) { throw; }
    catch(const json::exception&)
    {
        R_STHROW(r_http_400_exception, ("Malformed set_password request."));
    }
    catch(const std::exception& ex)
    {
        R_LOG_EXCEPTION_AT(ex, __FILE__, __LINE__);
    }
    R_STHROW(r_http_500_exception, ("Failed to set the local web password."));
}
