
#ifndef __vision_revere_api_h
#define __vision_revere_api_h

#include "r_utils/interfaces/r_socket_base.h"
#include "r_utils/r_nullable.h"
#include <memory>
#include <stdexcept>
#include <string>

namespace r_http { class r_client_request; }

namespace vision
{

// How vision reaches Revere's web API. Every request goes through these calls,
// so transport and credentials live in one place:
//
//   auto sok = revere_connect(host, timeout_ms);
//   r_client_request req(host, revere_port(host));
//   ...
//   authorize(req, host);
//   req.write_request(*sok);
//   resp.read_response(*sok);
//   note_status(host, resp.get_status());
//
// A Revere on this machine (loopback) is reached over plain HTTP and
// authenticated with the local API token Revere publishes in its data
// directory (r_utils/r_local_api_token.h).
//
// A remote Revere is reached over HTTPS. Its certificate is self-signed, so it
// is pinned on first use, as SSH does: the first time vision sees a host the
// connection is refused until the user confirms the fingerprint (Revere shows
// the same one), and afterwards a different certificate is refused until the
// user accepts the new one. Nothing, not even the password, is sent before
// that check. The session token from logging in with the password is kept per
// host, owner-only, in vision's data directory, so overlay windows (separate
// processes) and a restarted vision reuse it until it expires.

bool is_remote(const std::string& host);

// 8088 (plain HTTP, loopback) or 8443 (HTTPS, remote).
int revere_port(const std::string& host);

// Thrown by revere_connect() when a remote host's certificate isn't the pinned
// one (or nothing is pinned yet). The details are also left for the UI in
// pending_identity_problem().
class untrusted_server : public std::runtime_error
{
public:
    untrusted_server(const std::string& msg) : std::runtime_error(msg) {}
};

// A connected socket to host's API. Throws untrusted_server, or another
// exception if it can't connect.
std::unique_ptr<r_utils::r_socket_base> revere_connect(const std::string& host, uint64_t io_timeout_ms = 10000);

// Adds "Authorization: Bearer <token>" for host, if vision has a credential for it.
void authorize(r_http::r_client_request& req, const std::string& host);

// Feed every response status back: a 401 from a remote host means its session
// is gone, so it's dropped and session_needed() turns true.
void note_status(const std::string& host, int status);

// --- trust (remote hosts) ---

struct identity_problem
{
    std::string host;
    std::string presented;   // fingerprint the server showed
    std::string pinned;      // fingerprint vision expected; empty on first contact
};

// The most recent refused connection, until trusted or dismissed.
r_utils::r_nullable<identity_problem> pending_identity_problem();
// Pin fingerprint for host (first contact, or accepting a changed certificate).
void trust_server(const std::string& host, const std::string& fingerprint);
void dismiss_identity_problem();
r_utils::r_nullable<std::string> trusted_fingerprint(const std::string& host);
// Forget host's pinned certificate and its session.
void forget_server(const std::string& host);

// --- sign-in (remote hosts) ---

// True for a remote host vision has no session for.
bool session_needed(const std::string& host);

// Signs in to host with its password (blocking; network). Throws
// std::runtime_error with a message fit to show the user, or untrusted_server.
void login(const std::string& host, const std::string& password);

// --- video (RTSP) ---

// The credential for host's RTSP restream (r_vss/r_rtsp_auth.h), fetched from
// its API (GET /rtsp_credentials) on first use and then cached for this
// process. Null if it can't be had right now (not trusted / signed in yet,
// unreachable, or an older Revere without RTSP authentication); never throws.
struct rtsp_credentials
{
    std::string username;
    std::string password;
};
r_utils::r_nullable<rtsp_credentials> get_rtsp_credentials(const std::string& host);

// "host" from "rtsp://host:port/path" ("" if it isn't one).
std::string host_of_url(const std::string& url);

}

#endif
