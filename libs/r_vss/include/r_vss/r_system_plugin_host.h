
#ifndef _r_system_plugin_host_h
#define _r_system_plugin_host_h

#include "r_vss/r_system_plugin.h"
#include "r_utils/r_dynamic_library.h"
#include "r_utils/r_macro.h"

#include <list>
#include <memory>
#include <string>

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

private:
    struct plugin_info
    {
        std::unique_ptr<r_utils::r_dynamic_library> library;
        r_system_plugin_handle plugin_handle;
        void (*start_func)(r_system_plugin_handle);
        void (*stop_func)(r_system_plugin_handle);
        void (*destroy_func)(r_system_plugin_handle);
        std::string name;
    };

    std::string _top_dir;
    std::list<plugin_info> _plugins;
};

}

#endif
