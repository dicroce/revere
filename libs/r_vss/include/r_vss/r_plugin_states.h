#ifndef __r_vss_r_plugin_states_h
#define __r_vss_r_plugin_states_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace r_vss
{

// What plugins say about themselves, for the UIs beyond revere's own panel (set_plugin_state in the host API,
// r_system_plugin.h). Each plugin keeps one JSON object, replaced whole on every change, keyed by its GUID; a counter
// over all of them changes when any does, so revere_cloud knows with one read that something did.
// The only plugin that may read every plugin's state and run actions on them (get_plugin_states, plugin_action in the host
// API): the cloud's, which relays them to the app and the portal. Another plugin asking is refused.
constexpr const char* PLUGIN_STATES_READER_GUID = "com.revere.cloud";

class r_plugin_states
{
public:
    // The most a plugin's state may be, in bytes of its JSON text.
    static constexpr size_t MAX_STATE_BYTES = 256 * 1024;

    // Replaces the plugin's state. 0 on success, including when it is the same state as before (then nothing changes,
    // and the version doesn't move: a plugin may call it on every change without waking anyone). -1 when the text isn't
    // a JSON object, or is over MAX_STATE_BYTES; the previous state stays. "The same" is the same JSON, however it is
    // written (spacing, key order).
    R_API int set(const std::string& guid, const std::string& state_json);

    // The plugin's state is dropped, and the version moves if it had one: it was disabled, stopped or unloaded, so
    // what it said is no longer true.
    R_API void remove(const std::string& guid);

    // {"version": N, "plugins": {"<guid>": <state>, ...}}. N starts at 0 and rises by one with each change.
    R_API std::string json() const;

    R_API int64_t version() const;

private:
    mutable std::mutex _lock;
    int64_t _version {0};
    std::map<std::string, std::string> _states;     // each normalized: parsed and written back
};

}

#endif
