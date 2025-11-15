
#include "r_vss/r_system_plugin_host.h"
#include "r_utils/r_dynamic_library.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include <filesystem>

using namespace r_vss;
using namespace r_utils;
namespace fs = std::filesystem;

// C API function pointer types
typedef r_system_plugin_handle (*load_system_plugin_func)(const char*, system_plugin_log_func);
typedef void (*start_system_plugin_func)(r_system_plugin_handle);
typedef void (*stop_system_plugin_func)(r_system_plugin_handle);
typedef void (*destroy_system_plugin_func)(r_system_plugin_handle);

// Log callback wrapper
static void plugin_log_callback(const char* message)
{
    R_LOG_INFO("[SystemPlugin] %s", message);
}

r_system_plugin_host::r_system_plugin_host(const std::string& top_dir)
    : _top_dir(top_dir)
{
    R_LOG_INFO("r_system_plugin_host constructor called with top_dir=%s", top_dir.c_str());
 
    try
    {
        // Determine the plugin extension based on platform
#ifdef IS_WINDOWS
        const std::string plugin_ext = ".dll";
#elif defined(IS_MACOS)
        const std::string plugin_ext = ".dylib";
#else
        const std::string plugin_ext = ".so";
#endif

        std::string plugin_dir = r_fs::working_directory() + PATH_SLASH + "system_plugins";

        if (r_fs::is_dir(plugin_dir))
        {
            R_LOG_INFO("Searching for system plugins in: %s", plugin_dir.c_str());

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
                            // Extract plugin name (filename without extension)
                            std::string plugin_name = filename.substr(0, filename.size() - plugin_ext.size());

                            // Load the dynamic library
                            auto lib = std::make_unique<r_dynamic_library>(entry.path().string());

                            // Look for the required C API symbols
                            void* load_symbol = lib->resolve_symbol("load_system_plugin");
                            void* start_symbol = lib->resolve_symbol("start_system_plugin");
                            void* stop_symbol = lib->resolve_symbol("stop_system_plugin");
                            void* destroy_symbol = lib->resolve_symbol("destroy_system_plugin");

                            if (load_symbol && start_symbol && stop_symbol && destroy_symbol)
                            {
                                // Cast to function pointers
                                load_system_plugin_func load_func = reinterpret_cast<load_system_plugin_func>(load_symbol);
                                start_system_plugin_func start_func = reinterpret_cast<start_system_plugin_func>(start_symbol);
                                stop_system_plugin_func stop_func = reinterpret_cast<stop_system_plugin_func>(stop_symbol);
                                destroy_system_plugin_func destroy_func = reinterpret_cast<destroy_system_plugin_func>(destroy_symbol);

                                // Call load_system_plugin with top_dir and log callback
                                r_system_plugin_handle plugin_handle = load_func(_top_dir.c_str(), plugin_log_callback);

                                if (plugin_handle)
                                {
                                    _plugins.push_back({
                                        std::move(lib),
                                        plugin_handle,
                                        start_func,
                                        stop_func,
                                        destroy_func,
                                        plugin_name
                                    });
                                    R_LOG_INFO("Loaded system plugin: %s", filename.c_str());
                                }
                                else
                                {
                                    R_LOG_WARNING("System plugin %s load_plugin returned null", filename.c_str());
                                }
                            }
                            else
                            {
                                R_LOG_WARNING("System plugin %s does not export required symbols (load_system_plugin, start_system_plugin, stop_system_plugin, destroy_system_plugin)", filename.c_str());
                            }
                        }
                        catch (const std::exception& e)
                        {
                            R_LOG_WARNING("Failed to load system plugin %s: %s", filename.c_str(), e.what());
                        }
                    }
                }
            }
        }

        R_LOG_INFO("Loaded %zu system plugins", _plugins.size());
    }
    catch (const std::exception& e)
    {
        R_LOG_ERROR("Error loading system plugins: %s", e.what());
    }
}

r_system_plugin_host::~r_system_plugin_host()
{
    // Stop and destroy all plugins
    stop_all();

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
                R_LOG_ERROR("Error destroying system plugin %s: %s", p.name.c_str(), e.what());
            }
        }
    }
    _plugins.clear();
}

void r_system_plugin_host::start_all()
{
    for(auto& p : _plugins)
    {
        if (p.plugin_handle && p.start_func)
        {
            try
            {
                R_LOG_INFO("Starting system plugin: %s", p.name.c_str());
                p.start_func(p.plugin_handle);
            }
            catch (const std::exception& e)
            {
                R_LOG_ERROR("Error starting system plugin %s: %s", p.name.c_str(), e.what());
            }
        }
    }
}

void r_system_plugin_host::stop_all()
{
    for(auto& p : _plugins)
    {
        if (p.plugin_handle && p.stop_func)
        {
            try
            {
                R_LOG_INFO("Stopping system plugin: %s", p.name.c_str());
                p.stop_func(p.plugin_handle);
            }
            catch (const std::exception& e)
            {
                R_LOG_ERROR("Error stopping system plugin %s: %s", p.name.c_str(), e.what());
            }
        }
    }
}

std::vector<std::string> r_system_plugin_host::get_loaded_plugins() const
{
    std::vector<std::string> plugin_names;
    for(const auto& p : _plugins)
    {
        plugin_names.push_back(p.name);
    }
    return plugin_names;
}
