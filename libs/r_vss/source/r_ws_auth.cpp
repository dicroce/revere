#include "r_vss/r_ws_auth.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"

extern "C" {
#include <psa/crypto.h>
}

#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

namespace
{
// Throttle entries for addresses that have neither failed recently nor are
// locked out are dropped once the table reaches this size, so a scan from many
// addresses can't grow it without bound.
constexpr size_t THROTTLE_PRUNE_AT = 1024;
// A failure older than this no longer counts toward a lockout.
constexpr minutes FAILURE_MEMORY {15};
}

string r_ws_auth::generate_token()
{
    // psa_crypto_init() is idempotent and thread-safe (MBEDTLS_THREADING_C).
    if(psa_crypto_init() != PSA_SUCCESS)
        R_THROW(("psa_crypto_init() failed; cannot generate a token."));

    unsigned char bytes[32];
    if(psa_generate_random(bytes, sizeof(bytes)) != PSA_SUCCESS)
        R_THROW(("psa_generate_random() failed; cannot generate a token."));

    static const char* hex = "0123456789abcdef";
    string out;
    out.reserve(sizeof(bytes) * 2);
    for(auto b : bytes)
    {
        out.push_back(hex[b >> 4]);
        out.push_back(hex[b & 0x0f]);
    }
    return out;
}

void r_ws_auth::set_local_token(const string& token)
{
    lock_guard<mutex> g(_lock);
    _local_token = token;
}

string r_ws_auth::local_token() const
{
    lock_guard<mutex> g(_lock);
    return _local_token;
}

string r_ws_auth::issue_session(clock::time_point now)
{
    auto token = generate_token();
    lock_guard<mutex> g(_lock);
    _sessions[token] = now + SESSION_LIFETIME;
    return token;
}

bool r_ws_auth::valid(const string& token, clock::time_point now)
{
    if(token.empty())
        return false;

    lock_guard<mutex> g(_lock);

    if(!_local_token.empty() && constant_time_equal(token, _local_token))
        return true;

    auto it = _sessions.find(token);
    if(it == _sessions.end())
        return false;
    if(now > it->second)
    {
        _sessions.erase(it);
        return false;
    }
    return true;
}

void r_ws_auth::revoke(const string& token)
{
    lock_guard<mutex> g(_lock);
    _sessions.erase(token);
}

void r_ws_auth::revoke_all_sessions()
{
    lock_guard<mutex> g(_lock);
    _sessions.clear();
}

size_t r_ws_auth::session_count() const
{
    lock_guard<mutex> g(_lock);
    return _sessions.size();
}

bool r_ws_auth::locked_out(const string& address, clock::time_point now)
{
    lock_guard<mutex> g(_lock);
    auto it = _throttle.find(address);
    return it != _throttle.end() && now < it->second.locked_until;
}

void r_ws_auth::record_failure(const string& address, clock::time_point now)
{
    lock_guard<mutex> g(_lock);

    if(_throttle.size() >= THROTTLE_PRUNE_AT && _throttle.find(address) == _throttle.end())
        _prune_throttle(now);

    auto& e = _throttle[address];
    if(e.failures > 0 && now - e.last_failure > FAILURE_MEMORY)
        e.failures = 0;
    e.last_failure = now;
    if(++e.failures >= MAX_FAILED_LOGINS)
    {
        e.locked_until = now + LOCKOUT;
        e.failures = 0;
    }
}

void r_ws_auth::record_success(const string& address)
{
    lock_guard<mutex> g(_lock);
    _throttle.erase(address);
}

void r_ws_auth::_prune_throttle(clock::time_point now)
{
    for(auto it = _throttle.begin(); it != _throttle.end(); )
    {
        bool locked = now < it->second.locked_until;
        bool recent = now - it->second.last_failure <= FAILURE_MEMORY;
        if(!locked && !recent)
            it = _throttle.erase(it);
        else ++it;
    }
}

r_nullable<string> r_ws_auth::extract_token(const r_nullable<string>& authorization,
                                            const r_nullable<string>& cookie_header,
                                            bool cookie_allowed)
{
    r_nullable<string> out;

    if(!authorization.is_null())
    {
        const string prefix = "bearer ";
        auto val = r_string_utils::strip(authorization.value());
        if(val.size() > prefix.size() && r_string_utils::to_lower(val.substr(0, prefix.size())) == prefix)
        {
            auto token = r_string_utils::strip(val.substr(prefix.size()));
            if(!token.empty())
                out.set_value(token);
        }
        return out;
    }

    if(!cookie_allowed || cookie_header.is_null())
        return out;

    // Cookie: a=b; revere_session=<token>; c=d
    const string name = SESSION_COOKIE;
    for(auto& part : r_string_utils::split(cookie_header.value(), ';'))
    {
        auto kv = r_string_utils::strip(part);
        auto eq = kv.find('=');
        if(eq == string::npos)
            continue;
        if(r_string_utils::strip(kv.substr(0, eq)) != name)
            continue;
        auto token = r_string_utils::strip(kv.substr(eq + 1));
        if(token.size() >= 2 && token.front() == '"' && token.back() == '"')
            token = token.substr(1, token.size() - 2);
        if(!token.empty())
            out.set_value(token);
        return out;
    }

    return out;
}

string r_ws_auth::session_cookie(const string& token, bool secure)
{
    auto max_age = duration_cast<seconds>(SESSION_LIFETIME).count();
    return string(SESSION_COOKIE) + "=" + token +
           "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + to_string(max_age) +
           (secure ? "; Secure" : "");
}

string r_ws_auth::clear_session_cookie(bool secure)
{
    return string(SESSION_COOKIE) + "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0" +
           (secure ? "; Secure" : "");
}

bool r_ws_auth::constant_time_equal(const string& a, const string& b)
{
    if(a.size() != b.size())
        return false;
    unsigned char acc = 0;
    for(size_t i = 0; i < a.size(); ++i)
        acc |= (unsigned char)(a[i] ^ b[i]);
    return acc == 0;
}
