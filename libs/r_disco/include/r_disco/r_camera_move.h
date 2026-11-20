#ifndef r_disco_r_camera_move_h
#define r_disco_r_camera_move_h

#include "r_disco/r_camera.h"
#include "r_disco/r_stream_config.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <string>

namespace r_disco
{

// An assigned camera that discovery now finds at a different address.
//
// Following it means sending the camera's stored credentials to the new
// address, and recording whatever answers there under the camera's name. A
// camera's discovery id isn't proof (anything on the network can claim it, and
// some cameras share one), so revere follows a move only when the new address
// has the camera's stored hardware (MAC) address (decide_camera_move), and then
// at once: an address change never stops recording. Whether the old address
// still answers doesn't matter (another device taking it is often why the
// camera got a new one). Otherwise the move is held: the camera keeps its
// current address, and isn't recorded at the new one, until the user confirms
// the move or keeps the current one (r_agent::confirm_camera_move,
// dismiss_camera_move, answer_camera_move).
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
    // When the move was followed (status "followed"), else 0.
    int64_t followed_at_ms {0};
    // Why a held move isn't followed yet (hold_reason_for); empty until the
    // first check, and for a followed move.
    std::string hold_reason;
    // A followed move only: how the camera's MAC compared at the new address
    // (mac_check_for): "match", or, when the computer doesn't pause on an
    // address change (load_camera_move_policy), "changed", "unknown" or "none".
    // Empty for a move a person confirmed.
    std::string mac_check;
};

// What a move's hold_reason says about the camera's MAC at the new address:
// "" -> "match", "mac_mismatch" -> "changed", "mac_unknown" -> "unknown",
// "no_stored_mac" -> "none".
R_API std::string mac_check_for(const std::string& hold_reason);

// Whether this computer pauses recording a camera that turns up at a new
// address its MAC doesn't vouch for (no MAC stored, none readable there, a
// different one) until a person confirms. Off unless the cloud said otherwise:
// the camera keeps recording at the new address, and the move says how its MAC
// compared. A move whose MAC matches is followed either way.
//
// config/camera_moves.json: {"pause_on_address_change": false}. A missing
// file, bad JSON or a non-boolean value is off.
R_API bool parse_camera_move_policy(const std::string& json_text);
R_API std::string camera_move_policy_json(bool pause_on_address_change);
R_API bool load_camera_move_policy(const std::string& top_dir);
// Saves the choice; false when it couldn't be written.
R_API bool save_camera_move_policy(const std::string& top_dir, bool pause_on_address_change);

enum class move_outcome { hold, follow };

// Why a move is held, or empty when the new address has the camera's stored
// MAC and the move is followed:
//   "no_stored_mac"  no MAC is stored for the camera to check against
//   "mac_unknown"    no MAC could be read for the new address
//   "mac_mismatch"   the new address has a different MAC
R_API std::string hold_reason_for(const r_utils::r_nullable<std::string>& stored_mac,
                                  const r_utils::r_nullable<std::string>& new_mac);

// Follow only when the new address's MAC is the camera's stored MAC (in any
// spelling); hold_reason_for says why not.
R_API move_outcome decide_camera_move(const r_utils::r_nullable<std::string>& stored_mac,
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
