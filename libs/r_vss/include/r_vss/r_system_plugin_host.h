
#ifndef _r_system_plugin_host_h
#define _r_system_plugin_host_h

#include "r_vss/r_system_plugin.h"
#include "r_vss/r_plugin_manifest.h"
#include "r_utils/r_dynamic_library.h"
#include "r_utils/r_macro.h"

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace r_vss
{

class r_motion_event_plugin_host;
class r_frame_feeds;
struct r_detection;

typedef void (*system_plugin_post_detection_func)(r_system_plugin_handle, const char*, int64_t, const char*, float, float, float, float, float);
typedef void (*system_plugin_post_detection_v2_func)(r_system_plugin_handle, const char*, int64_t, const char*, float, float, float, float, float, int64_t);

// Hands one detection to a system plugin: by its system_plugin_post_detection_v2
// with posted_at_ms when it has one, else by system_plugin_post_detection, else not at all.
R_API void post_detection_to_plugin(r_system_plugin_handle plugin, system_plugin_post_detection_func v1,
                                    system_plugin_post_detection_v2_func v2, const r_detection& det, int64_t posted_at_ms);

// Where system plugins are looked for, in order; the first copy of a filename wins.
struct r_system_plugin_dirs
{
    std::vector<std::string> load;
    // A folder plugins may still sit in from before, but that can't load them
    // (the user data folder under the Snap); empty if none. Never loaded from,
    // only checked so a plugin found there gets a hint in the log.
    std::string unloadable;
};

// exe_dir: the folder with the executable. top_dir: the user data folder
// (~/Documents/revere/revere, LocalAppData/Revere). snap, snap_user_common: the
// $SNAP and $SNAP_USER_COMMON environment variables, or null when unset.
//
// The Snap's home plug lets it read the user data folder but not map code from
// it (AppArmor allows mmap exec only under ~/snap/revere), so under the Snap
// plugins are loaded from $SNAP_USER_COMMON/system_plugins instead.
R_API r_system_plugin_dirs system_plugin_dirs(const std::string& exe_dir,
                                              const std::string& top_dir,
                                              const char* snap,
                                              const char* snap_user_common);

// Whether this build loads only system plugins whose signed manifest checks out
// (check_system_plugin()): the REVERE_REQUIRE_SIGNED_PLUGINS build option, on for
// release tag builds. Off, a plugin that fails its check still loads, as before.
R_API bool signed_system_plugins_required();

// One plugin file's copies, in the order the folders are scanned (the exe's first),
// with what each copy's check found. The copy to load: the last that passed, so a
// verified copy in a user folder wins over the exe folder's; else, when signatures
// aren't required, the first, as before; else -1, none.
R_API int choose_system_plugin_copy(const std::vector<r_plugin_check_result>& checks, bool require_signed);

// A system plugin found but not loaded: no copy of it passed its check, or the copy chosen
// is for a plugin interface it doesn't load or couldn't be opened.
struct r_skipped_system_plugin
{
    std::string name;              // its file name without the extension, as get_loaded_plugins() names plugins
    std::string path;              // the copy reported: the last found, a user folder's if there is one
    r_plugin_check_result check;   // what that copy's check found

    // No manifest (an older installer), built for another revere or a plugin interface it doesn't load, or it can't
    // be opened here (it needs a library this revere doesn't have): "needs updating", and an
    // install of the matching build fixes it. Anything else failed its integrity check.
    bool needs_update() const
    {
        return check.status == r_plugin_check::no_manifest || check.status == r_plugin_check::wrong_version ||
               check.status == r_plugin_check::wrong_plugin_api || check.status == r_plugin_check::wont_load;
    }
};

// Deletes a plugin the user uninstalled (marked by mark_for_uninstall()): its manifest,
// signature and file, then the mark. False, with why, if the plugin file is still there,
// in which case the mark stays, the plugin isn't loaded, and the next start tries again.
R_API bool remove_uninstalled_system_plugin(const std::string& plugin_path, std::string& why);

class r_system_plugin_host
{
public:
    // Loads the plugins in system_plugin_dirs(), checked when signed_system_plugins_required().
    R_API r_system_plugin_host(const std::string& top_dir);
    // The same from dirs, checked when require_signed: for tests.
    R_API r_system_plugin_host(const std::string& top_dir, const r_system_plugin_dirs& dirs, bool require_signed);
    R_API ~r_system_plugin_host();

    // Loads one more plugin while revere runs (one just installed), checked as at start
    // and started if start_all() has run. False, with why, if it isn't loaded: it failed
    // its check, it's for a plugin interface this revere doesn't load or couldn't be opened (these three also
    // reported by get_skipped_plugins()), a plugin by its file name or GUID is already
    // loaded, or it isn't a system plugin. Nothing is ever unloaded:
    // dlclose under a plugin's own threads isn't safe.
    R_API bool load_and_start(const std::string& path, std::string& why);

    // The plugins found but not loaded, for the UI's "needs updating" or "failed its
    // integrity check": no copy passed its check (only when signatures are required), or,
    // in any build, the copy chosen is for a plugin interface it doesn't load or couldn't be opened.
    R_API std::vector<r_skipped_system_plugin> get_skipped_plugins() const;

    // The folder an in-app install writes to: the last plugin folder, the user's
    // (the data folder, or $SNAP_USER_COMMON under the Snap). A verified copy
    // there wins over one next to the exe.
    R_API std::string user_plugin_dir() const { return _user_dir; }

    // Whether the user can uninstall plugin_name from revere: its copy in the user's
    // plugin folder (where in-app installs go) is the one loaded, or the one skipped.
    // A copy anywhere else came from an installer, and is removed the same way.
    R_API bool can_uninstall(const std::string& plugin_name) const;

    // Marks that copy to be deleted, with its manifest and signature, at the next start
    // (plugin_uninstall_marker_path()); it isn't loaded then. Nothing is unloaded now: the
    // caller restarts revere. False, with why, if it can't be marked.
    R_API bool mark_for_uninstall(const std::string& plugin_name, std::string& why);

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

    // Get a plugin's version (empty if the plugin doesn't export get_system_plugin_version)
    R_API std::string get_plugin_version(const std::string& plugin_name) const;

    // Run an action from a plugin's status message (system_plugin_action).
    // False if the plugin isn't loaded or has no actions.
    R_API bool run_plugin_action(const std::string& plugin_name, const std::string& action);

    // Wire up detection events from the motion plugin host to all system plugins
    // that implement system_plugin_post_detection. Call after construction, before start_all().
    R_API void connect_detection_bus(r_motion_event_plugin_host& meph);

    // Set the callback invoked when a plugin delivers a cloud API result.
    // result_type identifies the payload; json is the raw result.
    R_API void set_api_result_fn(std::function<void(const std::string&, const std::string&)> fn);

    // Notify every plugin that implements system_plugin_cameras_changed that the
    // camera set changed (added / removed / re-assigned), so it can sync right
    // away instead of waiting for its next poll. Safe to call from the stream
    // keeper's reconciliation thread; plugins that don't implement it are skipped.
    R_API void notify_cameras_changed();

    // Gives plugins that export system_plugin_set_host_api the live-frame functions,
    // reading from feeds; camera_recording says whether a camera is recording here.
    // Call after construction, before start_all(). stop_all() closes their feeds.
    R_API void set_frame_source(r_frame_feeds* feeds, std::function<bool(const std::string&)> camera_recording);

private:
    struct plugin_info;
    std::string _uninstallable_path(const std::string& plugin_name) const;
    bool _load(const std::string& path, std::string& why);
    void _give_frames(plugin_info& p);
    void _skip(const r_skipped_system_plugin& skipped);
    std::vector<plugin_info*> _snapshot();
    std::vector<const plugin_info*> _snapshot() const;
    const plugin_info* _find(const std::string& plugin_name) const;
    plugin_info* _find(const std::string& plugin_name);

    void _dispatch_detection(const r_detection& det, int64_t posted_at_ms);
    void _on_api_result(const char* name, const char* json);
    static void _api_result_bridge(const char* result_type, const char* json, void* user_data);
    struct frame_api;
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
        system_plugin_post_detection_func post_detection_func;        // Optional - nullptr if not supported
        system_plugin_post_detection_v2_func post_detection_v2_func;  // Optional - nullptr if not supported; preferred over v1
        void (*set_api_result_cb_func)(r_system_plugin_handle, system_plugin_api_result_cb, void*);                   // Optional - nullptr if not supported
        void (*cameras_changed_func)(r_system_plugin_handle);        // Optional - nullptr if not supported
        void (*action_func)(r_system_plugin_handle, const char*);    // Optional - nullptr if not supported
        std::string name;
        std::string guid;
        std::string version;   // Empty if the plugin doesn't report one
        void (*set_host_api_func)(r_system_plugin_handle, const r_system_plugin_host_api*) = nullptr;   // Optional
        std::shared_ptr<frame_api> frames;   // its live-frame functions, once set_frame_source() gave them
        std::string path;   // the file it was loaded from
    };

    std::string _top_dir;
    std::string _user_dir;
    bool _require_signed {false};

    // _list_lock guards _plugins and _skipped. _plugins is only ever appended to (until the
    // destructor), and a std::list's elements never move, so a snapshot of pointers taken
    // under the lock stays good: plugins are called without it held.
    mutable std::mutex _list_lock;
    std::list<plugin_info> _plugins;
    std::vector<r_skipped_system_plugin> _skipped;

    // _ops_lock makes loading, start_all(), stop_all() and the setters one at a time, and
    // guards what only they touch.
    std::mutex _ops_lock;
    bool _started {false};
    std::set<std::string> _loaded_guids;       // Track loaded plugin GUIDs to prevent duplicates
    std::set<std::string> _loaded_filenames;   // Track loaded plugin filenames (dedupe BEFORE dlopen)
    std::function<void(const std::string&, const std::string&)> _api_result_fn;
    r_frame_feeds* _feeds {nullptr};
    std::function<bool(const std::string&)> _camera_recording;
};

}

#endif
