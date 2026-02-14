
#ifndef _r_system_plugin_h
#define _r_system_plugin_h

#include "r_utils/r_macro.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle
typedef void* r_system_plugin_handle;

// Required exports from each plugin shared library

// Get the plugin's unique identifier (GUID)
// This is called BEFORE load_system_plugin to check for duplicates
// Returns: A unique string identifier for this plugin (e.g., "com.example.myplugin")
// The returned string must remain valid for the lifetime of the loaded library
R_API const char* get_system_plugin_guid();

// Load and initialize the plugin
// Called once at Revere startup
// top_dir: Revere's top-level data directory
// logger_state: Pointer to host's r_logger::logger_state, plugin should call set_logger_state() with this
// Returns: Plugin handle (opaque pointer to plugin instance)
R_API r_system_plugin_handle load_system_plugin(const char* top_dir, void* logger_state);

// Start the plugin's operations
// Called after all plugins are loaded
R_API void start_system_plugin(r_system_plugin_handle plugin);

// Stop the plugin's operations
// Called during Revere shutdown
R_API void stop_system_plugin(r_system_plugin_handle plugin);

// Destroy and cleanup the plugin
// Called after stop, final cleanup before unloading
R_API void destroy_system_plugin(r_system_plugin_handle plugin);

// Check if the plugin is enabled
// Returns: true if enabled, false otherwise
R_API bool system_plugin_enabled(r_system_plugin_handle plugin);

// Enable or disable the plugin
// enabled: true to enable, false to disable
R_API void system_plugin_set_enabled(r_system_plugin_handle plugin, bool enabled);

// Optional: Get the plugin's current status string
// Returns: A status string (e.g., "disabled", "authenticating", "connected", "not_connected")
// The returned string must remain valid until the next call to this function
// Plugins that don't implement this export will have no status displayed
R_API const char* system_plugin_get_status(r_system_plugin_handle plugin);

// Optional: Get additional status detail (e.g., user code, URL during auth)
// Returns: A detail string, empty if no details available
R_API const char* system_plugin_get_status_message(r_system_plugin_handle plugin);

#ifdef __cplusplus
}
#endif

#endif
