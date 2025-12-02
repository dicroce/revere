
#ifndef _r_system_plugin_h
#define _r_system_plugin_h

#include "r_utils/r_macro.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle
typedef void* r_system_plugin_handle;

// Required exports from each plugin shared library

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

#ifdef __cplusplus
}
#endif

#endif
