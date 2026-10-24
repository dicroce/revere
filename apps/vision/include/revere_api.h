
#ifndef __vision_revere_api_h
#define __vision_revere_api_h

#include "r_utils/interfaces/r_socket_base.h"
#include <memory>
#include <string>

namespace r_http { class r_client_request; }

namespace vision
{

// How vision reaches Revere's web API. Every request goes through these three
// calls, so transport and credentials live in one place:
//
//   auto sok = revere_connect(host, timeout_ms);
//   r_client_request req(host, revere_port(host));
//   ...
//   authorize(req, host);
//   req.write_request(*sok);
//
// A Revere on this machine is reached over plain HTTP on loopback and
// authenticated with the local API token Revere publishes in its data directory
// (r_utils/r_local_api_token.h), which vision shares when run as the same user.
// A session token set with set_session_token() is used when there is no local
// token (a remote Revere, or one with a different data directory).

// The port to talk to on host.
int revere_port(const std::string& host);

// A connected socket to host's API. Throws on failure.
std::unique_ptr<r_utils::r_socket_base> revere_connect(const std::string& host, uint64_t io_timeout_ms = 10000);

// Adds "Authorization: Bearer <token>" for host, if vision has a credential for it.
void authorize(r_http::r_client_request& req, const std::string& host);

// The session token from logging in to a Revere with its password ("" to forget it).
void set_session_token(const std::string& token);

}

#endif
