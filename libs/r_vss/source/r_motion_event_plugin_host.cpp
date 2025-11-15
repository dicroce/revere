
#include "r_vss/r_motion_event_plugin_host.h"
#include "r_utils/r_dynamic_library.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include <filesystem>
#include <vector>

using namespace r_vss;
using namespace r_utils;
namespace fs = std::filesystem;

// C API function pointer types
typedef r_motion_plugin_handle (*load_plugin_func)(r_motion_event_plugin_host_handle);
typedef void (*destroy_plugin_func)(r_motion_plugin_handle);
typedef void (*post_motion_event_func)(r_motion_plugin_handle, int, const char*, int64_t, const uint8_t*, size_t, uint16_t, uint16_t, int, int, int, int, bool);

r_motion_event_plugin_host::r_motion_event_plugin_host(r_disco::r_devices& devices, const std::string& top_dir, r_stream_keeper& stream_keeper)
    : _devices(devices),
      _top_dir(top_dir),
      _stream_keeper(stream_keeper)
{
    try
    {
        // Get the current working directory and append "/motion_plugins"
        std::string plugin_dir = r_fs::working_directory() + "/motion_plugins";
        
        // Check if the directory exists
        if (!r_fs::is_dir(plugin_dir))
        {
            R_LOG_WARNING("Motion plugin directory does not exist: %s", plugin_dir.c_str());
            return;
        }
        
        // Determine the plugin extension based on platform
#ifdef IS_WINDOWS
        const std::string plugin_ext = ".dll";
#elif defined(IS_MACOS)
        const std::string plugin_ext = ".dylib";
#else
        const std::string plugin_ext = ".so";
#endif
        
        // Iterate through all files in the plugin directory
        for (const auto& entry : fs::directory_iterator(plugin_dir))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                
                // Check if file has the correct extension
                if (filename.size() > plugin_ext.size() && 
                    filename.substr(filename.size() - plugin_ext.size()) == plugin_ext)
                {
                    try
                    {
                        // Load the dynamic library
                        auto lib = std::make_unique<r_dynamic_library>(entry.path().string());

                        // Look for the required C API symbols
                        void* load_symbol = lib->resolve_symbol("load_plugin");
                        void* destroy_symbol = lib->resolve_symbol("destroy_plugin");
                        void* post_symbol = lib->resolve_symbol("post_motion_event");

                        if (load_symbol && destroy_symbol && post_symbol)
                        {
                            // Cast to function pointers
                            load_plugin_func load_func = reinterpret_cast<load_plugin_func>(load_symbol);
                            destroy_plugin_func destroy_func = reinterpret_cast<destroy_plugin_func>(destroy_symbol);
                            post_motion_event_func post_func = reinterpret_cast<post_motion_event_func>(post_symbol);

                            // Call load_plugin with host handle (this pointer cast to opaque handle)
                            r_motion_plugin_handle plugin_handle = load_func(reinterpret_cast<r_motion_event_plugin_host_handle>(this));

                            if (plugin_handle)
                            {
                                _plugins.push_back({std::move(lib), plugin_handle, destroy_func, post_func});
                                R_LOG_INFO("Loaded motion plugin: %s", filename.c_str());
                            }
                            else
                            {
                                R_LOG_WARNING("Plugin %s load_plugin returned null", filename.c_str());
                            }
                        }
                        else
                        {
                            R_LOG_WARNING("Plugin %s does not export required symbols (load_plugin, destroy_plugin, post_motion_event)", filename.c_str());
                        }
                    }
                    catch (const std::exception& e)
                    {
                        R_LOG_WARNING("Failed to load plugin %s: %s", filename.c_str(), e.what());
                    }
                }
            }
        }
        
        R_LOG_INFO("Loaded %zu motion plugins", _plugins.size());
    }
    catch (const std::exception& e)
    {
        R_LOG_ERROR("Error loading motion plugins: %s", e.what());
    }
}

r_motion_event_plugin_host::~r_motion_event_plugin_host()
{
    // Destroy all plugins using their destroy_plugin function
    for(auto& p : _plugins)
    {
        if (p.plugin_handle && p.destroy_func)
        {
            try
            {
                p.destroy_func(p.plugin_handle);
            }
            catch (const std::exception& e)
            {
                R_LOG_ERROR("Error destroying plugin: %s", e.what());
            }
        }
    }
    _plugins.clear();
}

void r_motion_event_plugin_host::post(r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height, const motion_region& motion_bbox)
{
    for(auto& p : _plugins)
    {
        if (p.plugin_handle && p.post_func)
        {
            // Call the C API post_motion_event function
            p.post_func(
                p.plugin_handle,
                static_cast<int>(evt),
                camera_id.c_str(),
                ts,
                frame_data.data(),
                frame_data.size(),
                width,
                height,
                motion_bbox.x,
                motion_bbox.y,
                motion_bbox.width,
                motion_bbox.height,
                motion_bbox.has_motion
            );
        }
    }
}
