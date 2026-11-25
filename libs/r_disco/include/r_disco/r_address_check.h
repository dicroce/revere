#ifndef r_disco_r_address_check_h
#define r_disco_r_address_check_h

#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <string>

namespace r_disco
{

// A device answers at an assigned camera's stored address, as that camera, with a hardware (MAC) address other than the
// one on file for it.
//
// Recording sends the camera's stored login to whatever answers at its address, so a device that took the address (a
// lapsed lease, or one that copies the camera's identity) would be handed it. r_camera_move.h guards a camera that
// moves; this guards one that stays: each discovery pass reads the MAC at the address of every camera it sees there
// (r_agent::check_stored_addresses). When it is not the camera's, the camera is held: its recording stops, and it is
// not started again, until a person confirms that it is their camera (r_agent::confirm_address_check, which learns
// the MAC seen), or the camera's own MAC is seen at the address again. A MAC that cannot be read says nothing either way.
struct r_address_check
{
    std::string ipv4;
    // When the different device was first seen, in milliseconds on the agent's clock. A person's confirmation names
    // it, so one given for an earlier device is not taken for a later one.
    int64_t since_ms {0};
};

// Whether the two MACs are both known and are not the same device's (the notations vary: case, separators).
R_API bool mac_differs(const r_utils::r_nullable<std::string>& stored, const r_utils::r_nullable<std::string>& seen);

// What the camera's card says while it is held.
R_API std::string address_check_message();

// How often a camera's MAC at its stored address is read: not held, and held (so a camera's own MAC coming back is
// noticed soon).
constexpr int64_t ADDRESS_CHECK_INTERVAL_MS = 5 * 60 * 1000;
constexpr int64_t ADDRESS_CHECK_HELD_INTERVAL_MS = 60 * 1000;

// r_agent::confirm_address_check's outcome: no_hold (nothing held for the camera), changed (the held device is another
// one than the person was shown).
enum class address_answer { ok, no_hold, changed };

}

#endif
