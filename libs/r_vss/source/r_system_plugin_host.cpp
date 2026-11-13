
#include "r_vss/r_system_plugin_host.h"
#include "r_vss/r_frame_feeds.h"
#include <algorithm>
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

void r_system_plugin_host::_give_frames(plugin_info& p)
{
    if(!_feeds || !p.plugin_handle || !p.set_host_api_func || p.frames)
        return;
    auto api = std::make_shared<frame_api>();
    api->feeds = _feeds;
    api->camera_recording = _camera_recording;
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

void r_system_plugin_host::set_frame_source(r_frame_feeds* feeds, std::function<bool(const std::string&)> camera_recording)
{
    std::lock_guard<std::mutex> g(_ops_lock);
    _feeds = feeds;
    _camera_recording = std::move(camera_recording);
    for(auto p : _snapshot())
        _give_frames(*p);
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

bool r_vss::signed_system_plugins_required()
{
#if defined(R_REQUIRE_SIGNED_PLUGINS) && R_REQUIRE_SIGNED_PLUGINS
    return true;
#else
    return false;
#endif
}

int r_vss::choose_system_plugin_copy(const std::vector<r_plugin_check_result>& checks, bool require_signed)
{
    for(int i = (int)checks.size() - 1; i >= 0; --i)
    {
        if(checks[i].ok())
            return i;
    }
    if(!require_signed && !checks.empty())
        return 0;
    return -1;
}

namespace
{

#ifdef IS_WINDOWS
const std::string PLUGIN_EXT = ".dll";
#elif defined(IS_MACOS)
const std::string PLUGIN_EXT = ".dylib";
#else
const std::string PLUGIN_EXT = ".so";
#endif

bool _has_plugin_ext(const std::string& filename)
{
    return filename.size() > PLUGIN_EXT.size() &&
           filename.compare(filename.size() - PLUGIN_EXT.size(), PLUGIN_EXT.size(), PLUGIN_EXT) == 0;
}

std::string _plugin_name(const std::string& filename)
{
    return filename.substr(0, filename.size() - PLUGIN_EXT.size());
}

// A symbol from lib as F, or nullptr if it doesn't export one.
template<typename F>
F _symbol(r_utils::r_dynamic_library& lib, const char* name)
{
    try
    {
        return reinterpret_cast<F>(lib.resolve_symbol(name));
    }
    catch (...)
    {
        return nullptr;
    }
}

}

r_system_plugin_host::r_system_plugin_host(const std::string& top_dir)
    : r_system_plugin_host(top_dir,
                           system_plugin_dirs(r_fs::working_directory(), top_dir, getenv("SNAP"), getenv("SNAP_USER_COMMON")),
                           signed_system_plugins_required())
{
}

r_system_plugin_host::r_system_plugin_host(const std::string& top_dir, const r_system_plugin_dirs& dirs, bool require_signed)
    : _top_dir(top_dir),
      _user_dir(dirs.load.empty() ? std::string() : dirs.load.back()),
      _require_signed(require_signed)
{
    R_LOG_INFO("r_system_plugin_host constructor called with top_dir=%s", top_dir.c_str());

    if(_require_signed && trusted_plugin_keys().empty())
        R_LOG_ERROR("This build loads only signed system plugins but trusts no signing key, so none will load.");

    try
    {
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
                if (it->is_regular_file(fec) && _has_plugin_ext(filename))
                {
                    R_LOG_ERROR("System plugin %s is in %s, where the Snap can't load it. Run its installer again, or move it to %s.",
                        filename.c_str(), dirs.unloadable.c_str(), dirs.load.back().c_str());
                }
            }
            if (ec && ec != std::errc::no_such_file_or_directory)
                R_LOG_WARNING("Could not check %s for left-over system plugins: %s", dirs.unloadable.c_str(), ec.message().c_str());
        }

        // Every copy of each plugin file, in scan order, before any is opened: the
        // same plugin can be both next to the exe and in the user folder, and which
        // copy loads depends on their checks. dlopen runs a library's static
        // initializers at once, so only the chosen copy is ever opened.
        std::vector<std::string> filenames;
        std::map<std::string, std::vector<std::string>> copies;
        for (const auto& plugin_dir : dirs.load)
        {
            if (!r_fs::is_dir(plugin_dir))
                continue;
            R_LOG_INFO("Searching for system plugins in: %s", plugin_dir.c_str());
            for (const auto& entry : fs::directory_iterator(plugin_dir))
            {
                if (!entry.is_regular_file())
                    continue;
                std::string filename = entry.path().filename().string();
                if (!_has_plugin_ext(filename))
                    continue;
                // Uninstalled by the user: deleted, not loaded, even if deleting fails.
                std::error_code mec;
                if (fs::exists(fs::path(plugin_uninstall_marker_path(entry.path().string())), mec))
                {
                    std::string why;
                    if (remove_uninstalled_system_plugin(entry.path().string(), why))
                        R_LOG_INFO("Uninstalled system plugin %s", entry.path().string().c_str());
                    else R_LOG_ERROR("System plugin %s was uninstalled, so it isn't loaded, but it couldn't be deleted (%s). Revere tries again at its next start.",
                                     entry.path().string().c_str(), why.c_str());
                    continue;
                }
                auto& paths = copies[filename];
                if (paths.empty())
                    filenames.push_back(filename);
                paths.push_back(entry.path().string());
            }
        }

        for (const auto& filename : filenames)
        {
            const auto& paths = copies[filename];
            std::vector<r_plugin_check_result> checks;
            for (const auto& path : paths)
                checks.push_back(check_system_plugin(path));

            if (_require_signed)
            {
                for (size_t i = 0; i < paths.size(); ++i)
                {
                    if (!checks[i].ok())
                        R_LOG_WARNING("System plugin %s failed its check (%s): %s", paths[i].c_str(),
                                      plugin_check_name(checks[i].status), checks[i].detail.c_str());
                }
            }

            int chosen = choose_system_plugin_copy(checks, _require_signed);
            if (chosen < 0)
            {
                r_skipped_system_plugin skipped;
                skipped.name = _plugin_name(filename);
                skipped.path = paths.back();
                skipped.check = checks.back();
                _skip(skipped);
                R_LOG_ERROR("System plugin %s not loaded: %s", filename.c_str(),
                            skipped.needs_update() ? "it needs updating for this version of revere" : "no copy passed its integrity check");
                continue;
            }

            if (paths.size() > 1)
                R_LOG_INFO("System plugin %s is in %zu folders; loading %s%s", filename.c_str(), paths.size(),
                           paths[chosen].c_str(), checks[chosen].ok() ? ", verified" : "");

            std::string why;
            if (!_load(paths[chosen], why))
                R_LOG_WARNING("System plugin %s not loaded: %s", filename.c_str(), why.c_str());
        }

        R_LOG_INFO("Loaded %zu system plugins", _plugins.size());
    }
    catch (const std::exception& e)
    {
        R_LOG_ERROR("Error loading system plugins: %s", e.what());
    }
}

bool r_system_plugin_host::_load(const std::string& path, std::string& why)
{
    std::string filename = fs::path(path).filename().string();
    if (!_has_plugin_ext(filename))
    {
        why = filename + " isn't a " + PLUGIN_EXT + " file";
        return false;
    }

    // Dedupe by filename BEFORE dlopen: loading a second copy of a plugin can
    // crash in its static initializers (e.g. a duplicate protobuf/OpenCV pulled in
    // by a differently-built copy) before we ever reach the GUID check below.
    if (_loaded_filenames.count(filename) > 0)
    {
        why = "a plugin named " + filename + " is already loaded";
        return false;
    }

    try
    {
        std::string plugin_name = _plugin_name(filename);

        auto lib = std::make_unique<r_dynamic_library>(path);
        _loaded_filenames.insert(filename);

        auto guid_func = _symbol<get_system_plugin_guid_func>(*lib, "get_system_plugin_guid");
        auto load_func = _symbol<load_system_plugin_func>(*lib, "load_system_plugin");
        auto start_func = _symbol<start_system_plugin_func>(*lib, "start_system_plugin");
        auto stop_func = _symbol<stop_system_plugin_func>(*lib, "stop_system_plugin");
        auto destroy_func = _symbol<destroy_system_plugin_func>(*lib, "destroy_system_plugin");
        auto enabled_func = _symbol<system_plugin_enabled_func>(*lib, "system_plugin_enabled");
        auto set_enabled_func = _symbol<system_plugin_set_enabled_func>(*lib, "system_plugin_set_enabled");
        if (!guid_func || !load_func || !start_func || !stop_func || !destroy_func || !enabled_func || !set_enabled_func)
        {
            why = filename + " does not export required symbols";
            return false;
        }

        // Optional exports: nullptr if not supported.
        auto status_func = _symbol<system_plugin_get_status_func>(*lib, "system_plugin_get_status");
        auto status_message_func = _symbol<system_plugin_get_status_message_func>(*lib, "system_plugin_get_status_message");
        auto post_detection_func = _symbol<system_plugin_post_detection_func>(*lib, "system_plugin_post_detection");
        auto post_detection_v2_func = _symbol<system_plugin_post_detection_v2_func>(*lib, "system_plugin_post_detection_v2");
        auto set_api_result_cb_func = _symbol<system_plugin_set_api_result_cb_func>(*lib, "system_plugin_set_api_result_cb");
        auto cameras_changed_func = _symbol<system_plugin_cameras_changed_func>(*lib, "system_plugin_cameras_changed");
        auto action_func = _symbol<void (*)(r_system_plugin_handle, const char*)>(*lib, "system_plugin_action");
        auto set_host_api_func = _symbol<void (*)(r_system_plugin_handle, const r_system_plugin_host_api*)>(*lib, "system_plugin_set_host_api");

        std::string plugin_version;
        auto version_func = _symbol<get_system_plugin_version_func>(*lib, "get_system_plugin_version");
        if (version_func)
        {
            const char* v = version_func();
            if (v)
                plugin_version = v;
        }

        const char* guid_cstr = guid_func();
        std::string plugin_guid = guid_cstr ? guid_cstr : "";
        if (plugin_guid.empty())
        {
            why = filename + " returned an empty GUID";
            return false;
        }
        if (_loaded_guids.count(plugin_guid) > 0)
        {
            why = "a plugin with GUID " + plugin_guid + " is already loaded";
            return false;
        }

        // Call load_system_plugin with top_dir and logger state
        r_system_plugin_handle plugin_handle = load_func(_top_dir.c_str(), r_logger::get_logger_state());
        if (!plugin_handle)
        {
            why = filename + "'s load_system_plugin returned null";
            return false;
        }

        _loaded_guids.insert(plugin_guid);
        plugin_info p {
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
        };
        p.set_host_api_func = set_host_api_func;
        p.path = path;

        // Wired up before anything else can see it.
        if (set_api_result_cb_func && _api_result_fn)
            set_api_result_cb_func(plugin_handle, _api_result_bridge, this);
        _give_frames(p);

        {
            std::lock_guard<std::mutex> g(_list_lock);
            _plugins.push_back(std::move(p));
            _skipped.erase(std::remove_if(_skipped.begin(), _skipped.end(),
                                          [&](const r_skipped_system_plugin& s) { return s.name == plugin_name; }),
                           _skipped.end());
        }

        R_LOG_INFO("Loaded system plugin: %s (GUID: %s, version: %s)", path.c_str(), plugin_guid.c_str(),
            plugin_version.empty() ? "unknown" : plugin_version.c_str());
        return true;
    }
    catch (const std::exception& e)
    {
        why = "failed to load " + filename + ": " + e.what();
        return false;
    }
}

void r_system_plugin_host::_skip(const r_skipped_system_plugin& skipped)
{
    std::lock_guard<std::mutex> g(_list_lock);
    for (auto& s : _skipped)
    {
        if (s.name == skipped.name)
        {
            s = skipped;
            return;
        }
    }
    _skipped.push_back(skipped);
}

bool r_system_plugin_host::load_and_start(const std::string& path, std::string& why)
{
    std::lock_guard<std::mutex> g(_ops_lock);

    std::string filename = fs::path(path).filename().string();
    if (_require_signed && _has_plugin_ext(filename))
    {
        auto check = check_system_plugin(path);
        if (!check.ok())
        {
            r_skipped_system_plugin skipped;
            skipped.name = _plugin_name(filename);
            skipped.path = path;
            skipped.check = check;
            _skip(skipped);
            why = std::string("it failed its check (") + plugin_check_name(check.status) + "): " + check.detail;
            R_LOG_ERROR("System plugin %s not loaded: %s", path.c_str(), why.c_str());
            return false;
        }
    }

    if (!_load(path, why))
    {
        R_LOG_WARNING("System plugin %s not loaded: %s", path.c_str(), why.c_str());
        return false;
    }

    if (_started)
    {
        auto p = _snapshot().back();   // the one just loaded: only loads append, and they hold _ops_lock
        try
        {
            R_LOG_INFO("Starting system plugin: %s", p->name.c_str());
            p->start_func(p->plugin_handle);
        }
        catch (const std::exception& e)
        {
            R_LOG_ERROR("Error starting system plugin %s: %s", p->name.c_str(), e.what());
        }
    }
    return true;
}

std::vector<r_skipped_system_plugin> r_system_plugin_host::get_skipped_plugins() const
{
    std::lock_guard<std::mutex> g(_list_lock);
    return _skipped;
}

std::vector<r_system_plugin_host::plugin_info*> r_system_plugin_host::_snapshot()
{
    std::lock_guard<std::mutex> g(_list_lock);
    std::vector<plugin_info*> out;
    for (auto& p : _plugins)
        out.push_back(&p);
    return out;
}

std::vector<const r_system_plugin_host::plugin_info*> r_system_plugin_host::_snapshot() const
{
    std::lock_guard<std::mutex> g(_list_lock);
    std::vector<const plugin_info*> out;
    for (const auto& p : _plugins)
        out.push_back(&p);
    return out;
}

const r_system_plugin_host::plugin_info* r_system_plugin_host::_find(const std::string& plugin_name) const
{
    std::lock_guard<std::mutex> g(_list_lock);
    for (const auto& p : _plugins)
    {
        if (p.name == plugin_name)
            return &p;
    }
    return nullptr;
}

r_system_plugin_host::plugin_info* r_system_plugin_host::_find(const std::string& plugin_name)
{
    std::lock_guard<std::mutex> g(_list_lock);
    for (auto& p : _plugins)
    {
        if (p.name == plugin_name)
            return &p;
    }
    return nullptr;
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
    std::lock_guard<std::mutex> g(_list_lock);
    _plugins.clear();
}

void r_system_plugin_host::start_all()
{
    std::lock_guard<std::mutex> g(_ops_lock);
    _started = true;
    for(auto p : _snapshot())
    {
        if (p->plugin_handle && p->start_func)
        {
            try
            {
                R_LOG_INFO("Starting system plugin: %s", p->name.c_str());
                p->start_func(p->plugin_handle);
            }
            catch (const std::exception& e)
            {
                R_LOG_ERROR("Error starting system plugin %s: %s", p->name.c_str(), e.what());
            }
        }
    }
}

void r_system_plugin_host::stop_all()
{
    std::lock_guard<std::mutex> g(_ops_lock);
    _started = false;
    for(auto p : _snapshot())
    {
        if (p->plugin_handle && p->stop_func)
        {
            try
            {
                R_LOG_INFO("Stopping system plugin: %s", p->name.c_str());
                p->stop_func(p->plugin_handle);
            }
            catch (const std::exception& e)
            {
                R_LOG_ERROR("Error stopping system plugin %s: %s", p->name.c_str(), e.what());
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
    for(auto p : _snapshot())
        plugin_names.push_back(p->name);
    return plugin_names;
}

bool r_vss::remove_uninstalled_system_plugin(const std::string& plugin_path, std::string& why)
{
    // The manifest first: without it a release build won't load what's left.
    for (const auto& path : {plugin_manifest_path(plugin_path), plugin_signature_path(plugin_path), plugin_path})
    {
        std::error_code ec;
        fs::remove(fs::path(path), ec);
        if (ec)
        {
            why = "couldn't delete " + path + ": " + ec.message();
            return false;
        }
    }
    std::error_code ec;
    fs::remove(fs::path(plugin_uninstall_marker_path(plugin_path)), ec);
    if (ec)
        R_LOG_WARNING("Uninstalled system plugin %s, but couldn't delete %s: %s", plugin_path.c_str(),
                      plugin_uninstall_marker_path(plugin_path).c_str(), ec.message().c_str());
    return true;
}

std::string r_system_plugin_host::_uninstallable_path(const std::string& plugin_name) const
{
    if (_user_dir.empty())
        return std::string();
    auto in_user_dir = [&](const std::string& path) {
        std::error_code ec;
        return !path.empty() && fs::equivalent(fs::path(path).parent_path(), fs::path(_user_dir), ec) && !ec;
    };
    std::lock_guard<std::mutex> g(_list_lock);
    for (const auto& p : _plugins)
    {
        if (p.name == plugin_name)
            return in_user_dir(p.path) ? p.path : std::string();
    }
    for (const auto& s : _skipped)
    {
        if (s.name == plugin_name)
            return in_user_dir(s.path) ? s.path : std::string();
    }
    return std::string();
}

bool r_system_plugin_host::can_uninstall(const std::string& plugin_name) const
{
    return !_uninstallable_path(plugin_name).empty();
}

bool r_system_plugin_host::mark_for_uninstall(const std::string& plugin_name, std::string& why)
{
    auto path = _uninstallable_path(plugin_name);
    if (path.empty())
    {
        why = plugin_name + " isn't installed in " + (_user_dir.empty() ? std::string("the user's plugin folder") : _user_dir);
        return false;
    }
    try
    {
        const std::string mark = "uninstall\n";
        r_fs::atomic_write_file((const uint8_t*)mark.data(), mark.size(), plugin_uninstall_marker_path(path));
    }
    catch (const std::exception& e)
    {
        why = std::string("couldn't mark it: ") + e.what();
        return false;
    }
    R_LOG_INFO("System plugin %s will be uninstalled at the next start (%s)", plugin_name.c_str(), path.c_str());
    return true;
}

bool r_system_plugin_host::is_plugin_enabled(const std::string& plugin_name) const
{
    auto p = _find(plugin_name);
    if (p && p->plugin_handle && p->enabled_func)
    {
        try
        {
            return p->enabled_func(p->plugin_handle);
        }
        catch (const std::exception& e)
        {
            R_LOG_ERROR("Error checking enabled state for plugin %s: %s", plugin_name.c_str(), e.what());
        }
    }
    return false;
}

void r_system_plugin_host::set_plugin_enabled(const std::string& plugin_name, bool enabled)
{
    auto p = _find(plugin_name);
    if (p && p->plugin_handle && p->set_enabled_func)
    {
        try
        {
            p->set_enabled_func(p->plugin_handle, enabled);
            R_LOG_INFO("Set plugin %s enabled=%s", plugin_name.c_str(), enabled ? "true" : "false");
        }
        catch (const std::exception& e)
        {
            R_LOG_ERROR("Error setting enabled state for plugin %s: %s", plugin_name.c_str(), e.what());
        }
    }
}

bool r_system_plugin_host::run_plugin_action(const std::string& plugin_name, const std::string& action)
{
    auto p = _find(plugin_name);
    if(!p || !p->plugin_handle || !p->action_func)
        return false;
    try
    {
        p->action_func(p->plugin_handle, action.c_str());
        R_LOG_INFO("Ran plugin %s action %s", plugin_name.c_str(), action.c_str());
        return true;
    }
    catch (const std::exception& e)
    {
        R_LOG_ERROR("Error running plugin %s action %s: %s", plugin_name.c_str(), action.c_str(), e.what());
        return false;
    }
}

std::string r_system_plugin_host::get_plugin_status(const std::string& plugin_name) const
{
    auto p = _find(plugin_name);
    if (p && p->plugin_handle && p->status_func)
    {
        try
        {
            const char* status = p->status_func(p->plugin_handle);
            return status ? std::string(status) : std::string();
        }
        catch (const std::exception& e)
        {
            R_LOG_ERROR("Error getting status for plugin %s: %s", plugin_name.c_str(), e.what());
        }
    }
    return std::string();
}

std::string r_system_plugin_host::get_plugin_version(const std::string& plugin_name) const
{
    auto p = _find(plugin_name);
    return p ? p->version : std::string();
}

std::string r_system_plugin_host::get_plugin_status_message(const std::string& plugin_name) const
{
    auto p = _find(plugin_name);
    if (p && p->plugin_handle && p->status_message_func)
    {
        try
        {
            const char* msg = p->status_message_func(p->plugin_handle);
            return msg ? std::string(msg) : std::string();
        }
        catch (const std::exception& e)
        {
            R_LOG_ERROR("Error getting status message for plugin %s: %s", plugin_name.c_str(), e.what());
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
    std::lock_guard<std::mutex> g(_ops_lock);
    _api_result_fn = std::move(fn);
    for(auto p : _snapshot())
    {
        if(p->set_api_result_cb_func && p->plugin_handle)
            p->set_api_result_cb_func(p->plugin_handle, _api_result_bridge, this);
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
    for(auto p : _snapshot())
    {
        if(p->cameras_changed_func && p->plugin_handle)
        {
            try { p->cameras_changed_func(p->plugin_handle); }
            catch(...) {}  // a plugin must never take the stream keeper down
        }
    }
}

void r_system_plugin_host::_dispatch_detection(const r_detection& det, int64_t posted_at_ms)
{
    for (auto p : _snapshot())
    {
        try
        {
            post_detection_to_plugin(p->plugin_handle, p->post_detection_func, p->post_detection_v2_func, det, posted_at_ms);
        }
        catch (const std::exception& e)
        {
            R_LOG_ERROR("Error dispatching detection to system plugin %s: %s", p->name.c_str(), e.what());
        }
    }
}
