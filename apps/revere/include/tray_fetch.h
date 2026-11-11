#ifndef __revere_tray_fetch_h
#define __revere_tray_fetch_h

#include "tray_status.h"
#include <string>

namespace revere
{

// GET /cameras from the Revere at host:port. It sends the local API token
// that Revere publishes in top_dir (r_utils/r_local_api_token.h), as Vision and
// the MCP bridge do, and reads it again on every call, because Revere replaces it
// each time it starts. With no token file, the request goes without one, and
// Revere answers 401. Never throws.
cameras_reply fetch_cameras(const std::string& host, int port, const std::string& top_dir, int timeout_ms = 3000);

}

#endif
