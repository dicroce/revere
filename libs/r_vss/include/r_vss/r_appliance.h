#ifndef r_vss_r_appliance_h
#define r_vss_r_appliance_h

#include "r_utils/r_macro.h"
#include "r_utils/3rdparty/json/json.h"
#include <string>

namespace r_vss
{

// Appliance mode: a dedicated Revere box that is set up from the Revere app
// (paired by the QR sticker's claim) instead of at a monitor. The box's
// provisioning step writes <top_dir>/appliance.json; without that file, or
// with "appliance" anything but true, nothing here changes how Revere behaves.
//
// In appliance mode the local web password belongs to the cloud: the box's
// agent sets it over loopback with the local API token, and the web UI only
// signs in with it.
namespace r_appliance
{

// <top_dir>/appliance.json
R_API std::string path(const std::string& top_dir);

// Whether <top_dir>/appliance.json says "appliance": true. Every other field
// (the claim secret among them) is ignored and never logged. False when the
// file is missing or unreadable. Never throws.
R_API bool read_flag(const std::string& top_dir);

// GET /auth_status: password_set, whether this is an appliance, and whether
// the caller is on this machine (loopback).
R_API nlohmann::json auth_status(bool password_set, bool appliance, bool local);

// How a /set_password request authenticated itself.
enum class set_password_auth
{
    none,               // no valid token, and no current_password (or a wrong one)
    current_password,   // the right current password, but no valid token
    session_token,      // a valid /login session token
    local_token         // the local API token (r_utils/r_local_api_token.h)
};

// What /set_password must answer before it changes anything.
enum class set_password_verdict
{
    allow,
    loopback_only,   // 403: the first password is set on this machine only
    unauthorized,    // 401: a change needs a token or the current password
    cloud_managed    // 403: an appliance's password is set only with the local token
};

// The rule /set_password applies. On a normal install the first password
// (none set yet) may be set without credentials, but only from loopback, and
// changing one that is set needs a valid token or the current password. On an
// appliance both need the local token (and the first, loopback too), so the
// cloud, through the box's agent, is the only owner of the password.
R_API set_password_verdict check_set_password(bool appliance, bool password_set, bool caller_loopback, set_password_auth auth);

// An appliance's local web password as sign-in compares it: upper case, with
// no '-' and no spaces, tabs or line breaks. The app shows the password in
// groups ("K7QM-X2PR-9DTF"); typed in lower case, without the hyphens or with
// spaces, it is the same password. Only ASCII letters change case.
R_API std::string normalize_password(const std::string& password);

// Whether a typed /login password is the stored one. On an appliance both are
// compared normalized (normalize_password), and a stored password that
// normalizes to nothing matches nothing. Everywhere else the comparison is
// exact. Both in constant time (r_ws_auth::constant_time_equal).
R_API bool login_password_matches(bool appliance, const std::string& typed, const std::string& stored);

}

}

#endif
