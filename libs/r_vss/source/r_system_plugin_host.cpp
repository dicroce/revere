
#include "r_vss/r_system_plugin_host.h"
#include "r_utils/r_dynamic_library.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include <filesystem>

using namespace r_vss;
using namespace r_utils;
namespace fs = std::filesystem;

// C API function pointer types
typedef const char* (*get_system_plugin_guid_func)();
typedef r_system_plugin_handle (*load_system_plugin_func)(const char*, void*);
typedef void (*start_system_plugin_func)(r_system_plugin_handle);
typedef void (*stop_system_plugin_func)(r_system_plugin_handle);
typedef void (*destroy_system_plugin_func)(r_system_plugin_handle);
typedef bool (*system_plugin_enabled_func)(r_system_plugin_handle);
typedef void (*system_plugin_set_enabled_func)(r_system_plugin_handle, bool);
typedef const char* (*system_plugin_get_status_func)(r_system_plugin_handle);
typedef const char* (*system_plugin_get_status_message_func)(r_system_plugin_handle);

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

        // Build list of plugin directories to search
        std::vector<std::string> plugin_dirs;

        // Primary location: alongside the executable
        plugin_dirs.push_back(r_fs::working_directory() + PATH_SLASH + "system_plugins");

        // Also check user data directory for downloaded plugins
        // (Documents/revere/revere on Linux/macOS, LocalAppData/Revere on Windows)
        plugin_dirs.push_back(_top_dir + PATH_SLASH + "system_plugins");

        for (const auto& plugin_dir : plugin_dirs)
        {
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
                            void* guid_symbol = lib->resolve_symbol("get_system_plugin_guid");
                            void* load_symbol = lib->resolve_symbol("load_system_plugin");
                            void* start_symbol = lib->resolve_symbol("start_system_plugin");
                            void* stop_symbol = lib->resolve_symbol("stop_system_plugin");
                            void* destroy_symbol = lib->resolve_symbol("destroy_system_plugin");
                            void* enabled_symbol = lib->resolve_symbol("system_plugin_enabled");
                            void* set_enabled_symbol = lib->resolve_symbol("system_plugin_set_enabled");

                            if (guid_symbol && load_symbol && start_symbol && stop_symbol && destroy_symbol &&
                                enabled_symbol && set_enabled_symbol)
                            {
                                // Cast to function pointers
                                get_system_plugin_guid_func guid_func = reinterpret_cast<get_system_plugin_guid_func>(guid_symbol);
                                load_system_plugin_func load_func = reinterpret_cast<load_system_plugin_func>(load_symbol);
                                start_system_plugin_func start_func = reinterpret_cast<start_system_plugin_func>(start_symbol);
                                stop_system_plugin_func stop_func = reinterpret_cast<stop_system_plugin_func>(stop_symbol);
                                destroy_system_plugin_func destroy_func = reinterpret_cast<destroy_system_plugin_func>(destroy_symbol);
                                system_plugin_enabled_func enabled_func = reinterpret_cast<system_plugin_enabled_func>(enabled_symbol);
                                system_plugin_set_enabled_func set_enabled_func = reinterpret_cast<system_plugin_set_enabled_func>(set_enabled_symbol);

                                // Optional: resolve status functions (nullptr if not supported)
                                system_plugin_get_status_func status_func = nullptr;
                                system_plugin_get_status_message_func status_message_func = nullptr;
                                try
                                {
                                    void* status_symbol = lib->resolve_symbol("system_plugin_get_status");
                                    if (status_symbol)
                                        status_func = reinterpret_cast<system_plugin_get_status_func>(status_symbol);
                                }
                                catch (...) {}
                                try
                                {
                                    void* msg_symbol = lib->resolve_symbol("system_plugin_get_status_message");
                                    if (msg_symbol)
                                        status_message_func = reinterpret_cast<system_plugin_get_status_message_func>(msg_symbol);
                                }
                                catch (...) {}

                                // Get the plugin GUID and check for duplicates
                                const char* guid_cstr = guid_func();
                                std::string plugin_guid = guid_cstr ? guid_cstr : "";

                                if (plugin_guid.empty())
                                {
                                    R_LOG_WARNING("System plugin %s returned empty GUID, skipping", filename.c_str());
                                    continue;
                                }

                                if (_loaded_guids.count(plugin_guid) > 0)
                                {
                                    R_LOG_INFO("System plugin %s (GUID: %s) already loaded, skipping duplicate",
                                        filename.c_str(), plugin_guid.c_str());
                                    continue;
                                }

                                // Call load_system_plugin with top_dir and logger state
                                r_system_plugin_handle plugin_handle = load_func(_top_dir.c_str(), r_logger::get_logger_state());

                                if (plugin_handle)
                                {
                                    _loaded_guids.insert(plugin_guid);
                                    _plugins.push_back({
                                        std::move(lib),
                                        plugin_handle,
                                        start_func,
                                        stop_func,
                                        destroy_func,
                                        enabled_func,
                                        set_enabled_func,
                                        status_func,
                                        status_message_func,
                                        plugin_name,
                                        plugin_guid
                                    });
                                    R_LOG_INFO("Loaded system plugin: %s (GUID: %s)", filename.c_str(), plugin_guid.c_str());
                                }
                                else
                                {
                                    R_LOG_WARNING("System plugin %s load_plugin returned null", filename.c_str());
                                }
                            }
                            else
                            {
                                R_LOG_WARNING("System plugin %s does not export required symbols", filename.c_str());
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
        } // end for each plugin_dir

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

bool r_system_plugin_host::is_plugin_enabled(const std::string& plugin_name) const
{
    for(const auto& p : _plugins)
    {
        if (p.name == plugin_name && p.plugin_handle && p.enabled_func)
        {
            try
            {
                return p.enabled_func(p.plugin_handle);
            }
            catch (const std::exception& e)
            {
                R_LOG_ERROR("Error checking enabled state for plugin %s: %s", plugin_name.c_str(), e.what());
            }
        }
    }
    return false;
}

void r_system_plugin_host::set_plugin_enabled(const std::string& plugin_name, bool enabled)
{
    for(auto& p : _plugins)
    {
        if (p.name == plugin_name && p.plugin_handle && p.set_enabled_func)
        {
            try
            {
                p.set_enabled_func(p.plugin_handle, enabled);
                R_LOG_INFO("Set plugin %s enabled=%s", plugin_name.c_str(), enabled ? "true" : "false");
            }
            catch (const std::exception& e)
            {
                R_LOG_ERROR("Error setting enabled state for plugin %s: %s", plugin_name.c_str(), e.what());
            }
            return;
        }
    }
}

std::string r_system_plugin_host::get_plugin_status(const std::string& plugin_name) const
{
    for(const auto& p : _plugins)
    {
        if (p.name == plugin_name && p.plugin_handle && p.status_func)
        {
            try
            {
                const char* status = p.status_func(p.plugin_handle);
                return status ? std::string(status) : std::string();
            }
            catch (const std::exception& e)
            {
                R_LOG_ERROR("Error getting status for plugin %s: %s", plugin_name.c_str(), e.what());
            }
        }
    }
    return std::string();
}

std::string r_system_plugin_host::get_plugin_status_message(const std::string& plugin_name) const
{
    for(const auto& p : _plugins)
    {
        if (p.name == plugin_name && p.plugin_handle && p.status_message_func)
        {
            try
            {
                const char* msg = p.status_message_func(p.plugin_handle);
                return msg ? std::string(msg) : std::string();
            }
            catch (const std::exception& e)
            {
                R_LOG_ERROR("Error getting status message for plugin %s: %s", plugin_name.c_str(), e.what());
            }
        }
    }
    return std::string();
}
