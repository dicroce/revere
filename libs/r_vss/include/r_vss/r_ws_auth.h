#ifndef r_vss_r_ws_auth_h
#define r_vss_r_ws_auth_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <chrono>
#include <map>
#include <mutex>
#include <string>

namespace r_vss
{

// Credential state for Revere's web API (r_ws), kept apart from the HTTP
// plumbing so it can be tested on its own.
//
// Two kinds of bearer token are accepted:
//   * session tokens, issued by /login for the system password, which expire
//     after SESSION_LIFETIME and are dropped when the password changes;
//   * the local token (r_utils/r_local_api_token.h), minted once per Revere run
//     for same-user processes on this machine (vision, the MCP bridge, system
//     plugins); it is never revoked by a password change.
//
// A token arrives either as "Authorization: Bearer <token>" or, for browsers
// (whose <img>, <video> and downloads can't set headers), as the
// SESSION_COOKIE cookie. The cookie is honoured only on safe methods (GET,
// HEAD): it is SameSite=Strict, and limiting it to reads means a forged
// cross-site form post can't use it to change anything even where SameSite
// isn't enforced. The web UI's writes always send the header.
//
// The login throttle is per client address: after MAX_FAILED_LOGINS
// consecutive wrong passwords from one address, that address is refused for
// LOCKOUT, whatever it sends. One noisy host can't lock everyone else out.
//
// Thread-safe.
class r_ws_auth final
{
public:
    using clock = std::chrono::steady_clock;

    static constexpr const char* SESSION_COOKIE = "revere_session";
    static constexpr std::chrono::hours SESSION_LIFETIME {24};
    static constexpr int MAX_FAILED_LOGINS = 5;
    static constexpr std::chrono::seconds LOCKOUT {30};

    R_API r_ws_auth() = default;
    r_ws_auth(const r_ws_auth&) = delete;
    r_ws_auth& operator=(const r_ws_auth&) = delete;

    // A fresh random token: 256 bits, hex.
    R_API static std::string generate_token();

    R_API void set_local_token(const std::string& token);
    R_API std::string local_token() const;

    R_API std::string issue_session(clock::time_point now = clock::now());
    // The local token, or an unexpired session token.
    R_API bool valid(const std::string& token, clock::time_point now = clock::now());
    // Ends a session (no effect on the local token).
    R_API void revoke(const std::string& token);
    R_API void revoke_all_sessions();
    R_API size_t session_count() const;

    // Login throttle, keyed by client address.
    R_API bool locked_out(const std::string& address, clock::time_point now = clock::now());
    R_API void record_failure(const std::string& address, clock::time_point now = clock::now());
    R_API void record_success(const std::string& address);

    // The token a request carries: the bearer header if present (even if
    // malformed, so a bad header isn't silently replaced by a cookie), else the
    // session cookie when cookie_allowed. Null if none.
    R_API static r_utils::r_nullable<std::string> extract_token(const r_utils::r_nullable<std::string>& authorization,
                                                               const r_utils::r_nullable<std::string>& cookie_header,
                                                               bool cookie_allowed);

    // Set-Cookie header values that set and clear the session cookie. Secure is
    // added when the response goes out over HTTPS.
    R_API static std::string session_cookie(const std::string& token, bool secure);
    R_API static std::string clear_session_cookie(bool secure);

    // Constant-time comparison (for passwords and tokens).
    R_API static bool constant_time_equal(const std::string& a, const std::string& b);

private:
    struct throttle_entry
    {
        int failures {0};
        clock::time_point last_failure {};
        clock::time_point locked_until {};
    };

    void _prune_throttle(clock::time_point now);

    mutable std::mutex _lock;
    std::string _local_token;
    std::map<std::string, clock::time_point> _sessions;   // token -> expiry
    std::map<std::string, throttle_entry> _throttle;      // address -> state
};

}

#endif
