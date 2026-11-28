#ifndef r_disco_r_camera_move_h
#define r_disco_r_camera_move_h

#include "r_disco/r_camera.h"
#include "r_disco/r_stream_config.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <map>
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
// current address, and isn't recorded at the new one, and nothing is sent to the
// new address, until the user confirms the move or keeps the current one
// (r_agent::confirm_camera_move, dismiss_camera_move, answer_camera_move).
//
// A different MAC at the new address is always held. A move that can't be
// checked (no MAC on file, or none readable there) is held too, unless the
// computer is set to follow unverified moves (load_camera_move_policy).
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
    // (mac_check_for): "match", or, for a move followed unverified (the computer
    // is set to, load_camera_move_policy), "unknown" or "none". Empty for a move
    // a person confirmed.
    std::string mac_check;
};

// What a move's hold_reason says about the camera's MAC at the new address:
// "" -> "match", "mac_unknown" -> "unknown", "mac_mismatch" -> "changed" (never followed),
// "no_stored_mac" -> "none".
R_API std::string mac_check_for(const std::string& hold_reason);

// Whether this computer follows a move it can't check, with no pause: no MAC
// stored for the camera, or none readable at the new address (a network where
// ARP can't be read). Off by default: such a move is held until a person
// confirms. A move to an address with a different MAC is held whatever this
// says, and a move whose MAC matches is followed either way.
//
// config/camera_moves.json: {"follow_unverified_moves": false}. A missing
// file, bad JSON or a non-boolean value is off. (An older file's
// "pause_on_address_change" is not read: it was off by default, and off must not
// become "follow".)
R_API bool parse_camera_move_policy(const std::string& json_text);
R_API std::string camera_move_policy_json(bool follow_unverified_moves);
R_API bool load_camera_move_policy(const std::string& top_dir);
// Saves the choice; false when it couldn't be written.
R_API bool save_camera_move_policy(const std::string& top_dir, bool follow_unverified_moves);

// Cameras followed to a new address, unverified, while no MAC was on
// file to check them by: camera id -> that address. While a camera is still
// there its MAC isn't noted, since nothing vouched that what answers there is
// the camera; a person's confirm, or a move elsewhere, ends it.
//
// config/camera_moves_unconfirmed.json: {"<camera id>": "<ipv4>"}. A missing
// file or bad JSON is none; entries that aren't strings are left out.
R_API std::map<std::string, std::string> parse_unconfirmed_addresses(const std::string& json_text);
R_API std::string unconfirmed_addresses_json(const std::map<std::string, std::string>& addresses);
R_API std::map<std::string, std::string> load_unconfirmed_addresses(const std::string& top_dir);
R_API bool save_unconfirmed_addresses(const std::string& top_dir, const std::map<std::string, std::string>& addresses);

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
