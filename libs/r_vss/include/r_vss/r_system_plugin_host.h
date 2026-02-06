
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

private:
    struct plugin_info
    {
        std::unique_ptr<r_utils::r_dynamic_library> library;
        r_system_plugin_handle plugin_handle;
        void (*start_func)(r_system_plugin_handle);
        void (*stop_func)(r_system_plugin_handle);
        void (*destroy_func)(r_system_plugin_handle);
        bool (*enabled_func)(r_system_plugin_handle);
        void (*set_enabled_func)(r_system_plugin_handle, bool);
        std::string name;
        std::string guid;
    };

    std::string _top_dir;
    std::list<plugin_info> _plugins;
    std::set<std::string> _loaded_guids;  // Track loaded plugin GUIDs to prevent duplicates
};

}

#endif
