#ifndef r_disco_r_camera_move_h
#define r_disco_r_camera_move_h

#include "r_disco/r_camera.h"
#include "r_disco/r_stream_config.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <chrono>
#include <cstdint>
#include <string>

namespace r_disco
{

// An assigned camera that discovery now finds at a different address.
//
// Following it means sending the camera's stored credentials to the new
// address, so revere follows only when it can tell the camera really moved
// (decide_camera_move): its old address has stopped answering for
// MOVE_FOLLOW_AFTER, and the new address has the camera's stored hardware (MAC)
// address. Otherwise the move is held: the camera keeps its current address,
// and records from it, until the user confirms the move
// (r_agent::confirm_camera_move) or keeps the current one
// (r_agent::dismiss_camera_move).
struct r_camera_move
{
    // "held" (waiting, or for the user) or "followed" (applied).
    std::string status {"held"};
    std::string old_ipv4;
    std::string new_ipv4;
    int old_port {0};
    int new_port {0};
    std::string old_xaddrs;
    std::string new_xaddrs;
    // When discovery first reported the new address, in milliseconds since the epoch.
    int64_t seen_at_ms {0};
    // Since when the old address hasn't answered (0: it answers, or not checked yet).
    int64_t old_dead_since_ms {0};
    // When the move was followed (status "followed"), else 0.
    int64_t followed_at_ms {0};
};

// How long the old address must stay silent before a move can be followed.
constexpr std::chrono::minutes MOVE_FOLLOW_AFTER {5};

enum class move_outcome { hold, follow };

// Follow only when every check passes: the old address has been silent for at
// least MOVE_FOLLOW_AFTER, and the new address's MAC is the camera's stored
// MAC. No stored MAC, or no MAC for the new address, means hold.
R_API move_outcome decide_camera_move(bool old_alive, std::chrono::milliseconds old_dead_for,
                                      const r_utils::r_nullable<std::string>& stored_mac,
                                      const r_utils::r_nullable<std::string>& new_mac);

// The move, if `fresh` (what discovery just reported for an assigned camera)
// puts the camera's ONVIF service at a different host, port or path than
// `stored`. Null for a camera that isn't assigned, has no stored address yet,
// or is where it was. A field discovery didn't report counts as unchanged.
R_API r_utils::r_nullable<r_camera_move> camera_move_from(const r_camera& stored, const r_stream_config& fresh);

// One line for the UI and logs: for a held move, that the camera answers at a
// new address and needs confirming; for a followed one, that recording continues.
R_API std::string camera_move_message(const r_camera_move& move);

}

#endif
