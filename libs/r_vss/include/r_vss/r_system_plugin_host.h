
#ifndef _r_system_plugin_host_h
#define _r_system_plugin_host_h

#include "r_vss/r_system_plugin.h"
#include "r_utils/r_dynamic_library.h"
#include "r_utils/r_macro.h"

#include <list>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace r_vss
{

class r_motion_event_plugin_host;
struct r_detection;

typedef void (*system_plugin_post_detection_func)(r_system_plugin_handle, const char*, int64_t, const char*, float, float, float, float, float);
typedef void (*system_plugin_post_detection_v2_func)(r_system_plugin_handle, const char*, int64_t, const char*, float, float, float, float, float, int64_t);

// Hands one detection to a system plugin: by its system_plugin_post_detection_v2
// with posted_at_ms when it has one, else by system_plugin_post_detection, else not at all.
R_API void post_detection_to_plugin(r_system_plugin_handle plugin, system_plugin_post_detection_func v1,
                                    system_plugin_post_detection_v2_func v2, const r_detection& det, int64_t posted_at_ms);

// Where system plugins are looked for, in order; the first copy of a filename wins.
struct r_system_plugin_dirs
{
    std::vector<std::string> load;
    // A folder plugins may still sit in from before, but that can't load them
    // (the user data folder under the Snap); empty if none. Never loaded from,
    // only checked so a plugin found there gets a hint in the log.
    std::string unloadable;
};

// exe_dir: the folder with the executable. top_dir: the user data folder
// (~/Documents/revere/revere, LocalAppData/Revere). snap, snap_user_common: the
// $SNAP and $SNAP_USER_COMMON environment variables, or null when unset.
//
// The Snap's home plug lets it read the user data folder but not map code from
// it (AppArmor allows mmap exec only under ~/snap/revere), so under the Snap
// plugins are loaded from $SNAP_USER_COMMON/system_plugins instead.
R_API r_system_plugin_dirs system_plugin_dirs(const std::string& exe_dir,
                                              const std::string& top_dir,
                                              const char* snap,
                                              const char* snap_user_common);

class r_system_plugin_host
{
public:
    R_API r_system_plugin_host(const std::string& top_dir);
    R_API ~r_system_plugin_host();

    // Start all loaded plugins
    R_API void start_all();

    // Stop all loaded plugins
    R_API void stop_all();

    // Get list of loaded plugin names
    R_API std::vector<std::string> get_loaded_plugins() const;

    // Check if a plugin is enabled by name
    R_API bool is_plugin_enabled(const std::string& plugin_name) const;

    // Enable or disable a plugin by name
    R_API void set_plugin_enabled(const std::string& plugin_name, bool enabled);

    // Get a plugin's current status JSON (empty if plugin doesn't support status reporting)
    R_API std::string get_plugin_status(const std::string& plugin_name) const;

    // Get a plugin's current status message JSON (empty if no message)
    R_API std::string get_plugin_status_message(const std::string& plugin_name) const;

    // Get a plugin's version (empty if the plugin doesn't export get_system_plugin_version)
    R_API std::string get_plugin_version(const std::string& plugin_name) const;

    // Run an action from a plugin's status message (system_plugin_action).
    // False if the plugin isn't loaded or has no actions.
    R_API bool run_plugin_action(const std::string& plugin_name, const std::string& action);

    // Wire up detection events from the motion plugin host to all system plugins
    // that implement system_plugin_post_detection. Call after construction, before start_all().
    R_API void connect_detection_bus(r_motion_event_plugin_host& meph);

    // Set the callback invoked when a plugin delivers a cloud API result.
    // result_type identifies the payload; json is the raw result.
    R_API void set_api_result_fn(std::function<void(const std::string&, const std::string&)> fn);

    // Notify every plugin that implements system_plugin_cameras_changed that the
    // camera set changed (added / removed / re-assigned), so it can sync right
    // away instead of waiting for its next poll. Safe to call from the stream
    // keeper's reconciliation thread; plugins that don't implement it are skipped.
    R_API void notify_cameras_changed();

private:
    void _dispatch_detection(const r_detection& det, int64_t posted_at_ms);
    void _on_api_result(const char* name, const char* json);
    static void _api_result_bridge(const char* result_type, const char* json, void* user_data);
    struct plugin_info
    {
        std::unique_ptr<r_utils::r_dynamic_library> library;
        r_system_plugin_handle plugin_handle;
        void (*start_func)(r_system_plugin_handle);
        void (*stop_func)(r_system_plugin_handle);
        void (*destroy_func)(r_system_plugin_handle);
        bool (*enabled_func)(r_system_plugin_handle);
        void (*set_enabled_func)(r_system_plugin_handle, bool);
        const char* (*status_func)(r_system_plugin_handle);          // Optional - nullptr if not supported
        const char* (*status_message_func)(r_system_plugin_handle);  // Optional - nullptr if not supported
        system_plugin_post_detection_func post_detection_func;        // Optional - nullptr if not supported
        system_plugin_post_detection_v2_func post_detection_v2_func;  // Optional - nullptr if not supported; preferred over v1
        void (*set_api_result_cb_func)(r_system_plugin_handle, system_plugin_api_result_cb, void*);                   // Optional - nullptr if not supported
        void (*cameras_changed_func)(r_system_plugin_handle);        // Optional - nullptr if not supported
        void (*action_func)(r_system_plugin_handle, const char*);    // Optional - nullptr if not supported
        std::string name;
        std::string guid;
        std::string version;   // Empty if the plugin doesn't report one
    };

    std::string _top_dir;
    std::list<plugin_info> _plugins;
    std::set<std::string> _loaded_guids;       // Track loaded plugin GUIDs to prevent duplicates
    std::set<std::string> _loaded_filenames;   // Track loaded plugin filenames (dedupe BEFORE dlopen)
    std::function<void(const std::string&, const std::string&)> _api_result_fn;
};

}

#endif
