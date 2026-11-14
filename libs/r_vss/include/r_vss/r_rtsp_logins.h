#ifndef r_vss_r_rtsp_logins_h
#define r_vss_r_rtsp_logins_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>

typedef struct _GstRTSPServer GstRTSPServer;
typedef struct _GstRTSPAuth GstRTSPAuth;

namespace r_vss
{

// Per-client RTSP logins for local plugins (see r_rtsp_auth.h for the shared one).
//
// A plugin that hands a video address to a LAN client asks for a login of that client's own, so it can take that
// client's video away later without changing the shared password for everyone else. A login is a random user name
// ("c-" and 12 hex digits) and password, accepted by Digest with the same viewer role as the shared one (video, nothing
// else). They live in memory only: a restart drops them, and the plugin asks again.
//
// Removing a login refuses its new requests (GstRTSPAuth takes logins added and removed while it runs), but that alone
// leaves a stream that is already playing running: gst-rtsp-server checks credentials when a stream is described and
// set up, not on each packet. So each login's token carries its name, the sessions that set up a stream under it are
// remembered, and removal ends them (their media stops and the session leaves the pool).
class r_rtsp_logins final
{
public:
    // Logins at one time. More than a home has clients; it only keeps a buggy caller from growing the table.
    static constexpr size_t MAX_LOGINS = 32;
    // The longest name a caller may give (what it shows in the list).
    static constexpr size_t MAX_NAME = 64;

    struct login_info
    {
        std::string username;
        std::string name;
    };

    struct new_login
    {
        std::string username;
        std::string password;
    };

    // `server` and `auth` must outlive this, and `auth` must be the server's.
    R_API r_rtsp_logins(GstRTSPServer* server, GstRTSPAuth* auth, size_t cap = MAX_LOGINS);
    R_API ~r_rtsp_logins() noexcept;

    r_rtsp_logins(const r_rtsp_logins&) = delete;
    r_rtsp_logins& operator=(const r_rtsp_logins&) = delete;

    // A new login named `name` (what the caller calls its client). Null when `cap` logins already exist.
    R_API r_utils::r_nullable<new_login> add(const std::string& name);

    // Removes a login and ends the sessions playing under it; false if there is none by that name.
    R_API bool remove(const std::string& username);

    // The logins, never their passwords.
    R_API std::vector<login_info> list() const;

    // Opaque (the server's callbacks share it).
    struct state;

private:
    std::shared_ptr<state> _state;
};

}

#endif
