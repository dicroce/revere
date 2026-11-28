#ifndef r_disco_r_address_check_h
#define r_disco_r_address_check_h

#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <map>
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
    // The different device's hardware (MAC) address, so a card can say which device is held and a person sees a replaced
    // one is not the one they looked at.
    std::string seen_mac;
};

// Whether the two MACs are both known and are not the same device's (the notations vary: case, separators).
R_API bool mac_differs(const r_utils::r_nullable<std::string>& stored, const r_utils::r_nullable<std::string>& seen);

// What the stream keeper does with a camera, given whether it is running and whether it is held: a held camera that is
// running is stopped (its recording context removed), one that is not running is not started (its login is never sent
// to the device at the address), and anything else is left alone. The keeper asks this at both places it decides, so
// the rule is tested without a keeper (which binds ports).
enum class address_hold_action { none, stop, do_not_start };
R_API address_hold_action address_hold_action_for(bool running, bool held);

// What the camera's card says while it is held.
R_API std::string address_check_message();

// The line that says which device: its MAC (empty when not known).
R_API std::string address_check_device_line(const r_address_check& check);

// What is kept of a hold across a restart (config/address_holds.json): the stream keeper would otherwise connect to
// the camera's address, with its login, before the first check after startup could find the device again.
struct r_address_hold_record
{
    std::string ipv4;
    int64_t since_ms {0};
    std::string seen_mac;
};
R_API std::map<std::string, r_address_hold_record> parse_address_holds(const std::string& json_text);
R_API std::string address_holds_json(const std::map<std::string, r_address_hold_record>& holds);
// An absent or unreadable file is no holds.
R_API std::map<std::string, r_address_hold_record> load_address_holds(const std::string& top_dir);
R_API bool save_address_holds(const std::string& top_dir, const std::map<std::string, r_address_hold_record>& holds);

// How often a camera's MAC at its stored address is read: not held, and held (so a camera's own MAC coming back is
// noticed soon).
constexpr int64_t ADDRESS_CHECK_INTERVAL_MS = 5 * 60 * 1000;
constexpr int64_t ADDRESS_CHECK_HELD_INTERVAL_MS = 60 * 1000;

// r_agent::confirm_address_check's outcome: no_hold (nothing held for the camera), changed (the held device is another
// one than the person was shown).
enum class address_answer { ok, no_hold, changed };

}

#endif
