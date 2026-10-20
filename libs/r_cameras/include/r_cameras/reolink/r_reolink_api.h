
#ifndef __r_cameras_reolink_r_reolink_api_h
#define __r_cameras_reolink_r_reolink_api_h

#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <map>
#include <string>

namespace r_cameras
{

// Reolink's own HTTP API (/cgi-bin/api.cgi), the parts Revere uses: the
// camera's motion and AI (person, vehicle, animal...) alarm states. Reolinks
// keep answering it when their ONVIF service has stopped: Driveway on
// 2026-10-20, ONVIF unreachable, answered these in under 100 ms.
//
// Credentials go as URL parameters on each request (one command per request),
// which works on both our firmwares (RLC-810A builds 22041503 and 2411261176).
// The token login (cmd=Login) succeeds, but Porch's firmware then refuses the
// token ("please login first") for polls and even for Logout; URL credentials
// need no session to expire or leak. Plain HTTP either way: Reolink's login
// sends the password in the clear too.

// What one poll found.
struct reolink_state
{
    bool motion {false};
    // AI alarm types the camera supports ("people", "vehicle", "dog_cat",
    // "face", "package"...) and whether each is active now.
    std::map<std::string, bool> ai;
};

// GetMdState and GetAiState responses, parsed into one state. Throws
// std::runtime_error on one it can't use, with the camera's error detail
// when it gave one ("please login first" for wrong credentials).
R_API reolink_state reolink_parse_state(const std::string& md_json, const std::string& ai_json);

class r_reolink_client
{
public:
    R_API r_reolink_client(const std::string& host, const r_utils::r_nullable<std::string>& username,
                           const r_utils::r_nullable<std::string>& password, int port = 80);

    // Motion and AI states now (two requests).
    R_API reolink_state poll();

private:
    std::string _get(const std::string& cmd);

    std::string _host;
    int _port;
    std::string _username;
    std::string _password;
};

}

#endif
