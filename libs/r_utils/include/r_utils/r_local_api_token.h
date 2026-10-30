#ifndef r_utils_r_local_api_token_h
#define r_utils_r_local_api_token_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <string>

namespace r_utils
{

// The local API token: how processes on the same machine, running as the same
// user, authenticate to Revere's web API without the system password.
//
// Revere mints a fresh random token at startup and writes it to an owner-only
// file in its per-user data directory (the "top dir"); it is valid only while
// that Revere process runs, and the file is removed on shutdown. Vision, the
// MCP bridge and in-process system plugins read it and send it as
// "Authorization: Bearer <token>". Anything able to read the file could
// already read Revere's recordings and keys in the same directory, so it grants
// no access the user's account didn't already have, while keeping other users
// and other machines out.
namespace r_local_api_token
{

// <top_dir>/local_api_token
R_API std::string path(const std::string& top_dir);

// Atomically (re)writes the token file, readable by its owner only.
R_API void write(const std::string& top_dir, const std::string& token);

// The current token, or null if there is none (Revere isn't running, or uses a
// different data directory). Never throws.
R_API r_nullable<std::string> read(const std::string& top_dir);

// Best effort; never throws.
R_API void remove(const std::string& top_dir);

// Revere's default per-user data directory (what it uses without --data-dir):
//   Windows: %LOCALAPPDATA%\revere\revere
//   Linux:   ~/Documents/revere/revere
//   macOS:   ~/Library/Application Support/Revere/revere
// Does not create it. Empty if it can't be determined.
R_API std::string default_top_dir();

// True for the hosts that mean "this machine": 127.0.0.1 (any 127/8), ::1, localhost.
R_API bool is_loopback_host(const std::string& host);

// "Bearer <token>" for an Authorization header.
R_API std::string bearer(const std::string& token);

}

}

#endif
