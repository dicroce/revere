
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

    // Wire up detection events from the motion plugin host to all system plugins
    // that implement system_plugin_post_detection. Call after construction, before start_all().
    R_API void connect_detection_bus(r_motion_event_plugin_host& meph);

    // Set the callback invoked when a plugin delivers a cloud API result.
    // result_type identifies the payload; json is the raw result.
    R_API void set_api_result_fn(std::function<void(const std::string&, const std::string&)> fn);

private:
    void _dispatch_detection(const r_detection& det);
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
        void (*post_detection_func)(r_system_plugin_handle, const char*, int64_t, const char*, float, float, float, float, float);  // Optional - nullptr if not supported
        void (*set_api_result_cb_func)(r_system_plugin_handle, system_plugin_api_result_cb, void*);                   // Optional - nullptr if not supported
        std::string name;
        std::string guid;
    };

    std::string _top_dir;
    std::list<plugin_info> _plugins;
    std::set<std::string> _loaded_guids;       // Track loaded plugin GUIDs to prevent duplicates
    std::set<std::string> _loaded_filenames;   // Track loaded plugin filenames (dedupe BEFORE dlopen)
    std::function<void(const std::string&, const std::string&)> _api_result_fn;
};

}

#endif
