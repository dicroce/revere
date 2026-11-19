#ifndef r_vss_r_web_password_h
#define r_vss_r_web_password_h

#include "r_vss/r_ws_auth.h"
#include "r_http/r_server_request.h"
#include "r_http/r_server_response.h"
#include "r_utils/interfaces/r_socket_base.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <cstdint>
#include <string>
#include <vector>

namespace r_vss
{

// The local web password (stored encrypted as <top_dir>/system_password) and
// the two open routes that manage it, GET /auth_status and POST /set_password.
// r_ws registers these handlers as they are; they live here, apart from r_ws's
// servers, cameras and plugins, so tests can drive them with real requests.
//
// The appliance flag (r_vss/r_appliance.h) is read from <top_dir>/appliance.json
// once, when this is constructed, and decides the /set_password rule.
class r_web_password final
{
public:
    R_API r_web_password(const std::string& top_dir, r_ws_auth& auth);
    r_web_password(const r_web_password&) = delete;
    r_web_password& operator=(const r_web_password&) = delete;

    // The key the password is encrypted with (from r_secure_store). Until it's
    // set no password can be stored or read, so nobody can sign in.
    R_API void set_master_key(const std::vector<uint8_t>& key);

    R_API bool appliance() const;

    R_API bool password_set() const;
    // Null if none is set or it can't be decrypted with the master key.
    R_API r_utils::r_nullable<std::string> load();
    // Throws if there's no master key.
    R_API void store(const std::string& password);
    R_API void clear();

    // GET /auth_status: {password_set, appliance, local}.
    R_API r_http::r_server_response get_auth_status(r_utils::r_socket_base& conn, const r_http::r_server_request& request);
    // POST /set_password {new_password, current_password?}: the rule is
    // r_appliance::check_set_password. On success every session is revoked.
    R_API r_http::r_server_response post_set_password(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    // The bearer token or (for GET/HEAD) session cookie a request carries.
    R_API static r_utils::r_nullable<std::string> request_token(const r_http::r_server_request& request);
    // The client's address, or "" if it can't be read.
    R_API static std::string peer_ip(r_utils::r_socket_base& conn);
    // True when conn is an HTTPS connection.
    R_API static bool is_tls(r_utils::r_socket_base& conn);

private:
    std::string _path() const;

    std::string _top_dir;
    r_ws_auth& _auth;
    bool _appliance {false};
    std::vector<uint8_t> _master_key;
};

}

#endif
