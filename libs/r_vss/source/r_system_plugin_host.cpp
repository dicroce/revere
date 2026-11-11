
#include "r_vss/r_system_plugin_host.h"
#include "r_vss/r_frame_feeds.h"
#include <cstddef>
#include <mutex>
#include <map>
#include "r_vss/r_motion_event_plugin_host.h"
#include "r_utils/r_dynamic_library.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include <cstdlib>
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
typedef const char* (*get_system_plugin_version_func)();
typedef void (*system_plugin_set_api_result_cb_func)(r_system_plugin_handle, system_plugin_api_result_cb, void*);
typedef void (*system_plugin_cameras_changed_func)(r_system_plugin_handle);

void r_system_plugin_host::_api_result_bridge(const char* name, const char* json, void* user_data)
{
    reinterpret_cast<r_system_plugin_host*>(user_data)->_on_api_result(name, json);
}

// One live-frame feed a plugin holds: its r_frame_feeds id, and the frame its
// last next_frame returned, whose data stays valid until the next call.
struct r_frame_subscription
{
    uint64_t id {0};
    std::string camera_id;
    r_feed_frame current;
};

// A plugin's live-frame functions (r_system_plugin_host_api), and its feeds.
struct r_system_plugin_host::frame_api
{
    // A feed's bound is raised to this, so a key frame always fits; and a plugin has at
    // most MAX_FEEDS feeds, each holding up to its bound.
    static constexpr size_t MIN_FEED_BYTES = 1024 * 1024;
    static constexpr size_t MAX_FEEDS = 16;

    r_frame_feeds* feeds {nullptr};
    std::function<bool(const std::string&)> camera_recording;
    std::string guid;
    r_system_plugin_host_api table {};
    // The plugin's feeds by handle. next() holds its own reference while it reads, so an
    // unsubscribe from another thread meanwhile only drops the map's: the last one frees it.
    std::mutex lock;
    std::map<r_frame_subscription*, std::shared_ptr<r_frame_subscription>> subs;

    static r_frame_subscription_handle subscribe(void* host, const char* camera_id, const char* stream, uint32_t media, size_t max_queued_bytes)
    {
        return subscribe_from(host, camera_id, stream, media, max_queued_bytes, 0);
    }

    static r_frame_subscription_handle subscribe_from(void* host, const char* camera_id, const char* stream, uint32_t media, size_t max_queued_bytes, uint32_t flags)
    {
        if((flags & ~(uint32_t)R_SUBSCRIBE_FROM_NEWEST_KEY) != 0)
            return nullptr;
        bool from_newest_key = (flags & R_SUBSCRIBE_FROM_NEWEST_KEY) != 0;
        auto api = static_cast<frame_api*>(host);
        if(!api || !camera_id || !api->feeds)
            return nullptr;
        std::string camera = camera_id;
        std::string s = stream ? stream : "";
        if(api->camera_recording && !api->camera_recording(camera))
            return nullptr;
        if(max_queued_bytes != 0 && max_queued_bytes < MIN_FEED_BYTES)
            max_queued_bytes = MIN_FEED_BYTES;
        r_frame_subscription* sub = nullptr;
        {
            // Checked and added under one lock, so concurrent subscribes can't pass the cap.
            std::lock_guard<std::mutex> g(api->lock);
            if(api->subs.size() >= MAX_FEEDS)
            {
                R_LOG_WARNING("System plugin %s: no frame feed for camera %s: it has %zu already", api->guid.c_str(), camera.c_str(), api->subs.size());
                return nullptr;
            }
            auto id = api->feeds->subscribe(camera, s, media, max_queued_bytes, from_newest_key);
            if(id == 0)
                return nullptr;
            auto owned = std::make_shared<r_frame_subscription>();
            owned->id = id;
            owned->camera_id = camera;
            sub = owned.get();
            api->subs[sub] = std::move(owned);
        }
        R_LOG_INFO("System plugin %s: frame feed for camera %s (%s%s%s%s)", api->guid.c_str(), camera.c_str(),
                   s.empty() ? "main" : s.c_str(), (media & R_FRAME_VIDEO) ? ", video" : "", (media & R_FRAME_AUDIO) ? ", audio" : "",
                   from_newest_key ? ", from the newest key frame" : "");
        return sub;
    }

    static int next(void* host, r_frame_subscription_handle sub, int timeout_ms, r_frame* frame)
    {
        auto api = static_cast<frame_api*>(host);
        if(!api || !sub || !frame || !api->feeds)
            return -1;
        std::shared_ptr<r_frame_subscription> s;
        {
            std::lock_guard<std::mutex> g(api->lock);
            auto it = api->subs.find(sub);
            if(it == api->subs.end())
                return -1;   // unsubscribed
            s = it->second;
        }
        auto r = api->feeds->next(s->id, timeout_ms, s->current);
        if(r != 1)
            return r;
        // Only the fields that fit in the plugin's r_frame: an older plugin's is smaller.
        auto fits = [frame](size_t offset, size_t size) { return frame->struct_size >= offset + size; };
        auto& c = s->current;
        if(fits(offsetof(r_frame, flags), sizeof(frame->flags))) frame->flags = c.flags;
        if(fits(offsetof(r_frame, ts), sizeof(frame->ts))) frame->ts = c.ts;
        if(fits(offsetof(r_frame, data), sizeof(frame->data))) frame->data = c.data.data();
        if(fits(offsetof(r_frame, size), sizeof(frame->size))) frame->size = c.data.size();
        if(fits(offsetof(r_frame, codec_name), sizeof(frame->codec_name))) frame->codec_name = c.codec_name.c_str();
        if(fits(offsetof(r_frame, codec_params), sizeof(frame->codec_params))) frame->codec_params = c.codec_params.c_str();
        if(fits(offsetof(r_frame, stream), sizeof(frame->stream))) frame->stream = c.stream.c_str();
        return 1;
    }

    static void unsubscribe(void* host, r_frame_subscription_handle sub)
    {
        auto api = static_cast<frame_api*>(host);
        if(!api || !sub)
            return;
        std::shared_ptr<r_frame_subscription> s;
        {
            std::lock_guard<std::mutex> g(api->lock);
            auto it = api->subs.find(sub);
            if(it == api->subs.end())
                return;
            s = std::move(it->second);
            api->subs.erase(it);
        }
        // Wakes a next() waiting on it, which still holds its own reference.
        if(api->feeds)
            api->feeds->unsubscribe(s->id);
        R_LOG_INFO("System plugin %s: frame feed for camera %s ended", api->guid.c_str(), s->camera_id.c_str());
    }
};

void r_system_plugin_host::set_frame_source(r_frame_feeds* feeds, std::function<bool(const std::string&)> camera_recording)
{
    _feeds = feeds;
    for(auto& p : _plugins)
    {
        if(!p.plugin_handle || !p.set_host_api_func || p.frames)
            continue;
        auto api = std::make_shared<frame_api>();
        api->feeds = feeds;
        api->camera_recording = camera_recording;
        api->guid = p.guid;
        api->table.struct_size = sizeof(r_system_plugin_host_api);
        api->table.version = R_SYSTEM_PLUGIN_HOST_API_VERSION;
        api->table.host = api.get();
        api->table.subscribe_frames = &frame_api::subscribe;
        api->table.next_frame = &frame_api::next;
        api->table.unsubscribe = &frame_api::unsubscribe;
        api->table.subscribe_frames_from = &frame_api::subscribe_from;
        p.frames = api;
        try
        {
            p.set_host_api_func(p.plugin_handle, &p.frames->table);
        }
        catch (const std::exception& e)
        {
            R_LOG_ERROR("Error giving system plugin %s its host functions: %s", p.name.c_str(), e.what());
        }
    }
}

r_system_plugin_dirs r_vss::system_plugin_dirs(const std::string& exe_dir,
                                                const std::string& top_dir,
                                                const char* snap,
                                                const char* snap_user_common)
{
    r_system_plugin_dirs dirs;

    // Primary location: alongside the executable (inside $SNAP under the Snap).
    dirs.load.push_back(exe_dir + PATH_SLASH + "system_plugins");

    const std::string user_dir = top_dir + PATH_SLASH + "system_plugins";
    if(snap != nullptr && snap_user_common != nullptr && snap_user_common[0] != '\0')
    {
        // Under the Snap: plugins installed for this user, where the Snap may map code.
        dirs.load.push_back(std::string(snap_user_common) + PATH_SLASH + "system_plugins");
        dirs.unloadable = user_dir;
    }
    else
    {
        // User data directory for downloaded plugins
        // (Documents/revere/revere on Linux/macOS, LocalAppData/Revere on Windows)
        dirs.load.push_back(user_dir);
    }

    return dirs;
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

        auto dirs = system_plugin_dirs(r_fs::working_directory(), _top_dir, getenv("SNAP"), getenv("SNAP_USER_COMMON"));
        const auto& plugin_dirs = dirs.load;

        // A plugin left where this install can't load it (installed for the
        // Snap before it had a folder of its own): say where it belongs. Only a
        // hint, so nothing here may stop the real plugins loading: error_code
        // overloads throughout (a folder can stat but not list).
        if (!dirs.unloadable.empty())
        {
            std::error_code ec;
            fs::directory_iterator it(dirs.unloadable, ec), end;
            for (; !ec && it != end; it.increment(ec))
            {
                std::error_code fec;
                std::string filename = it->path().filename().string();
                if (it->is_regular_file(fec) && filename.size() > plugin_ext.size() &&
                    filename.substr(filename.size() - plugin_ext.size()) == plugin_ext)
                {
                    R_LOG_ERROR("System plugin %s is in %s, where the Snap can't load it. Run its installer again, or move it to %s.",
                        filename.c_str(), dirs.unloadable.c_str(), dirs.load.back().c_str());
                }
            }
            if (ec && ec != std::errc::no_such_file_or_directory)
                R_LOG_WARNING("Could not check %s for left-over system plugins: %s", dirs.unloadable.c_str(), ec.message().c_str());
        }

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
                            // Dedupe by filename BEFORE dlopen. The same plugin can
                            // exist in both search dirs (e.g. shipped next to the exe
                            // AND installed in the user data dir). dlopen runs the
                            // library's static initializers immediately, so loading a
                            // second copy can crash (e.g. a duplicate protobuf/OpenCV
                            // pulled in by a differently-built copy) before we ever
                            // reach the GUID check below. Skipping by filename here
                            // means the first dir scanned (next-to-exe) wins.
                            if (_loaded_filenames.count(filename) > 0)
                            {
                                R_LOG_INFO("System plugin %s already loaded from an earlier directory, skipping duplicate", filename.c_str());
                                continue;
                            }

                            // Extract plugin name (filename without extension)
                            std::string plugin_name = filename.substr(0, filename.size() - plugin_ext.size());

                            // Load the dynamic library
                            auto lib = std::make_unique<r_dynamic_library>(entry.path().string());
                            _loaded_filenames.insert(filename);

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
                                try {
                                    void* status_symbol = lib->resolve_symbol("system_plugin_get_status");
                                    if (status_symbol)
                                        status_func = reinterpret_cast<system_plugin_get_status_func>(status_symbol);
                                } catch (...) {}
                                try {
                                    void* msg_symbol = lib->resolve_symbol("system_plugin_get_status_message");
                                    if (msg_symbol)
                                        status_message_func = reinterpret_cast<system_plugin_get_status_message_func>(msg_symbol);
                                } catch (...) {}

                                system_plugin_post_detection_func post_detection_func = nullptr;
                                try {
                                    void* pd_symbol = lib->resolve_symbol("system_plugin_post_detection");
                                    if (pd_symbol)
                                        post_detection_func = reinterpret_cast<system_plugin_post_detection_func>(pd_symbol);
                                } catch (...) {}
                                system_plugin_post_detection_v2_func post_detection_v2_func = nullptr;
                                try {
                                    void* sym = lib->resolve_symbol("system_plugin_post_detection_v2");
                                    if (sym)
                                        post_detection_v2_func = reinterpret_cast<system_plugin_post_detection_v2_func>(sym);
                                } catch (...) {}

                                system_plugin_set_api_result_cb_func set_api_result_cb_func = nullptr;
                                try {
                                    void* sym = lib->resolve_symbol("system_plugin_set_api_result_cb");
                                    if (sym)
                                        set_api_result_cb_func = reinterpret_cast<system_plugin_set_api_result_cb_func>(sym);
                                } catch (...) {}

                                system_plugin_cameras_changed_func cameras_changed_func = nullptr;
                                try {
                                    void* sym = lib->resolve_symbol("system_plugin_cameras_changed");
                                    if (sym)
                                        cameras_changed_func = reinterpret_cast<system_plugin_cameras_changed_func>(sym);
                                } catch (...) {}

                                void (*action_func)(r_system_plugin_handle, const char*) = nullptr;
                                try {
                                    void* sym = lib->resolve_symbol("system_plugin_action");
                                    if (sym)
                                        action_func = reinterpret_cast<void (*)(r_system_plugin_handle, const char*)>(sym);
                                } catch (...) {}

                                void (*set_host_api_func)(r_system_plugin_handle, const r_system_plugin_host_api*) = nullptr;
                                try {
                                    void* sym = lib->resolve_symbol("system_plugin_set_host_api");
                                    if (sym)
                                        set_host_api_func = reinterpret_cast<void (*)(r_system_plugin_handle, const r_system_plugin_host_api*)>(sym);
                                } catch (...) {}

                                // Optional: the plugin's own version
                                std::string plugin_version;
                                try {
                                    void* sym = lib->resolve_symbol("get_system_plugin_version");
                                    if (sym)
                                    {
                                        const char* v = reinterpret_cast<get_system_plugin_version_func>(sym)();
                                        if (v)
                                            plugin_version = v;
                                    }
                                } catch (...) {}

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
                                        post_detection_func,
                                        post_detection_v2_func,
                                        set_api_result_cb_func,
                                        cameras_changed_func,
                                        action_func,
                                        plugin_name,
                                        plugin_guid,
                                        plugin_version
                                    });
                                    _plugins.back().set_host_api_func = set_host_api_func;

                                    if(set_api_result_cb_func && _api_result_fn)
                                        set_api_result_cb_func(plugin_handle, _api_result_bridge, this);
                                    R_LOG_INFO("Loaded system plugin: %s (GUID: %s, version: %s)", filename.c_str(), plugin_guid.c_str(),
                                        plugin_version.empty() ? "unknown" : plugin_version.c_str());
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
        // Feeds a plugin never unsubscribed: closed when it stopped, freed now.
        if(p.frames)
        {
            std::lock_guard<std::mutex> g(p.frames->lock);
            for(auto& sub : p.frames->subs)
                if(_feeds)
                    _feeds->unsubscribe(sub.second->id);
            p.frames->subs.clear();
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
    // A plugin stopping unsubscribes its feeds; any left are closed, so a next_frame
    // on them returns -1 at once. They're freed when the plugin is destroyed.
    if(_feeds)
        _feeds->close_all();
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

bool r_system_plugin_host::run_plugin_action(const std::string& plugin_name, const std::string& action)
{
    for(auto& p : _plugins)
    {
        if(p.name != plugin_name)
            continue;
        if(!p.plugin_handle || !p.action_func)
            return false;
        try
        {
            p.action_func(p.plugin_handle, action.c_str());
            R_LOG_INFO("Ran plugin %s action %s", plugin_name.c_str(), action.c_str());
            return true;
        }
        catch (const std::exception& e)
        {
            R_LOG_ERROR("Error running plugin %s action %s: %s", plugin_name.c_str(), action.c_str(), e.what());
            return false;
        }
    }
    return false;
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

std::string r_system_plugin_host::get_plugin_version(const std::string& plugin_name) const
{
    for(const auto& p : _plugins)
    {
        if (p.name == plugin_name)
            return p.version;
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

void r_system_plugin_host::connect_detection_bus(r_motion_event_plugin_host& meph)
{
    meph.register_detection_consumer(
        [](void* userdata, const r_detection& det, int64_t posted_at_ms) {
            reinterpret_cast<r_system_plugin_host*>(userdata)->_dispatch_detection(det, posted_at_ms);
        },
        this
    );
}

void r_system_plugin_host::set_api_result_fn(std::function<void(const std::string&, const std::string&)> fn)
{
    _api_result_fn = std::move(fn);
    for(auto& p : _plugins)
    {
        if(p.set_api_result_cb_func && p.plugin_handle)
            p.set_api_result_cb_func(p.plugin_handle, _api_result_bridge, this);
    }
}

void r_system_plugin_host::_on_api_result(const char* name, const char* json)
{
    if(_api_result_fn)
        _api_result_fn(name ? name : "", json ? json : "");
}

void r_vss::post_detection_to_plugin(r_system_plugin_handle plugin, system_plugin_post_detection_func v1,
                                     system_plugin_post_detection_v2_func v2, const r_detection& det, int64_t posted_at_ms)
{
    if (!plugin)
        return;
    if (v2)
        v2(plugin, det.camera_id.c_str(), det.ts, det.class_name.c_str(), det.confidence,
           det.x1, det.y1, det.x2, det.y2, posted_at_ms);
    else if (v1)
        v1(plugin, det.camera_id.c_str(), det.ts, det.class_name.c_str(), det.confidence,
           det.x1, det.y1, det.x2, det.y2);
}

void r_system_plugin_host::notify_cameras_changed()
{
    for(auto& p : _plugins)
    {
        if(p.cameras_changed_func && p.plugin_handle)
        {
            try { p.cameras_changed_func(p.plugin_handle); }
            catch(...) {}  // a plugin must never take the stream keeper down
        }
    }
}

void r_system_plugin_host::_dispatch_detection(const r_detection& det, int64_t posted_at_ms)
{
    for (auto& p : _plugins)
    {
        try
        {
            post_detection_to_plugin(p.plugin_handle, p.post_detection_func, p.post_detection_v2_func, det, posted_at_ms);
        }
        catch (const std::exception& e)
        {
            R_LOG_ERROR("Error dispatching detection to system plugin %s: %s", p.name.c_str(), e.what());
        }
    }
}
