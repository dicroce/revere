#include "r_vss/r_appliance.h"
#include "r_vss/r_ws_auth.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"

using namespace r_vss;
using namespace r_utils;
using namespace std;
using json = nlohmann::json;

string r_appliance::path(const string& top_dir)
{
    return top_dir + PATH_SLASH + "appliance.json";
}

bool r_appliance::read_flag(const string& top_dir)
{
    auto p = path(top_dir);
    try
    {
        if(!r_fs::file_exists(p))
            return false;
        auto bytes = r_fs::read_file(p);
        // Don't throw on a bad file: the message would quote its contents,
        // and those include the claim secret.
        auto j = json::parse(string(bytes.begin(), bytes.end()), nullptr, false);
        if(!j.is_object() || !j.contains("appliance") || !j["appliance"].is_boolean())
        {
            R_LOG_WARNING("r_appliance: %s has no boolean \"appliance\"; appliance mode stays off", p.c_str());
            return false;
        }
        return j["appliance"].get<bool>();
    }
    catch(...)
    {
        R_LOG_WARNING("r_appliance: couldn't read %s; appliance mode stays off", p.c_str());
        return false;
    }
}

json r_appliance::auth_status(bool password_set, bool appliance, bool local)
{
    json j;
    j["password_set"] = password_set;
    j["appliance"] = appliance;
    j["local"] = local;
    return j;
}

r_appliance::set_password_verdict r_appliance::check_set_password(bool appliance, bool password_set, bool caller_loopback, set_password_auth auth)
{
    // Bootstrap: the first password needs no credentials, but only from this
    // machine. Otherwise anyone who could reach the HTTPS port of an
    // unconfigured install could claim it. An appliance also wants the local
    // token, which its agent holds: no other process or loopback request can
    // claim an unclaimed box first.
    if(!password_set)
    {
        if(!caller_loopback)
            return set_password_verdict::loopback_only;
        if(appliance && auth != set_password_auth::local_token)
            return set_password_verdict::cloud_managed;
        return set_password_verdict::allow;
    }

    if(appliance)
        return auth == set_password_auth::local_token ? set_password_verdict::allow : set_password_verdict::cloud_managed;

    return auth == set_password_auth::none ? set_password_verdict::unauthorized : set_password_verdict::allow;
}

string r_appliance::normalize_password(const string& password)
{
    string out;
    out.reserve(password.size());
    for(char c : password)
    {
        if(c == '-' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f')
            continue;
        out.push_back((c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c);
    }
    return out;
}

bool r_appliance::login_password_matches(bool appliance, const string& typed, const string& stored)
{
    if(!appliance)
        return r_ws_auth::constant_time_equal(typed, stored);
    auto want = normalize_password(stored);
    if(want.empty())
        return false;
    return r_ws_auth::constant_time_equal(normalize_password(typed), want);
}
