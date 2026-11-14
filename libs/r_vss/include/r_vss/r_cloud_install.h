#ifndef _r_vss_r_cloud_install_h
#define _r_vss_r_cloud_install_h

#include "r_vss/r_plugin_installer.h"
#include "r_vss/r_system_plugin_host.h"
#include "r_utils/r_macro.h"
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace r_vss
{

// Installing Revere Cloud from inside revere, for the desktop and web UIs
// (project-planning docs/IN_APP_PLUGIN_INSTALL.md, sections 3, 5, 6 and 7): the
// install runs in the background, and its state is read by polling status().

// The terms an install asks the user to accept first. Change the version when the
// terms change, and the next install or update asks again.
constexpr const char* REVERE_CLOUD_TERMS_URL = "https://revere.video/terms";
constexpr const char* REVERE_CLOUD_TERMS_VERSION = "2026-11-07";

// The plugin's name, as get_loaded_plugins() names it.
constexpr const char* REVERE_CLOUD_PLUGIN_NAME = "revere_cloud_system_plugin";

// A release build's version: plain "x.y.z". A release build installs the plugin
// from its own release. A dev build with a base version (release_base_version())
// installs it, for testing, from the newest release at or below that base, with a
// warning; any other build's UI keeps opening the download page.
R_API bool is_release_version(const std::string& version);

enum class r_cloud_state
{
    not_installed,   // no Revere Cloud plugin found
    installing,      // an install is running
    installed,       // loaded
    needs_update,    // found but not loaded: no manifest, or built for another revere
    failed_check     // found but not loaded: it failed its integrity check
};

// "not_installed", "installing", ... for status JSON.
R_API const char* cloud_state_name(r_cloud_state state);

struct r_cloud_install_status
{
    r_cloud_state state {r_cloud_state::not_installed};
    std::string detail;            // why it isn't loaded (needs_update, failed_check)
    bool can_install {false};      // an install (or update) may start: a release or dev build, not loaded, not installing
    // A dev build's warning that the plugin comes from a release, for testing; empty
    // for a release build.
    std::string dev_warning;
    bool terms_accepted {false};   // the current terms version was accepted before
    // The last install's failure, until another starts; empty kind if none failed.
    std::string error_kind;        // plugin_install_name(): "not_available", "download_failed", ...
    std::string error_detail;
};

class r_cloud_install
{
public:
    // What it works with, as functions so tests can give fakes.
    struct deps
    {
        std::string install_dir;   // the user plugin folder (r_system_plugin_host::user_plugin_dir())
        std::string terms_path;    // where the terms acceptance is kept (a json file in top_dir)
        bool release_build {false};
        std::string revere_version;     // this build's, for a dev build's warning
        std::string dev_base_version;   // a dev build's release_base_version(); empty for a release build
        std::function<r_plugin_install_result(const std::string& dir)> install;   // install_revere_cloud_plugin
        std::function<bool(const std::string& path, std::string& why)> load;     // r_system_plugin_host::load_and_start
        std::function<std::vector<std::string>()> loaded;                       // get_loaded_plugins
        std::function<std::vector<r_skipped_system_plugin>()> skipped;          // get_skipped_plugins
    };

    R_API explicit r_cloud_install(const deps& d);
    R_API ~r_cloud_install();   // waits for a running install

    r_cloud_install(const r_cloud_install&) = delete;
    r_cloud_install& operator=(const r_cloud_install&) = delete;

    R_API r_cloud_install_status status() const;

    // Starts an install in the background: downloads the plugin into the user
    // plugin folder, checks it, and loads it. accepted_terms_version must be the
    // current REVERE_CLOUD_TERMS_VERSION, unless that version was accepted before
    // (an update doesn't ask again); it's recorded with the time. False, with why,
    // if it can't start: neither a release nor a dev build, already loaded or installing, or the
    // terms not accepted.
    R_API bool start(const std::string& accepted_terms_version, std::string& why);

private:
    void _run();
    bool _terms_accepted_locked() const;
    bool _can_install_build() const;

    deps _deps;
    mutable std::mutex _lock;
    bool _installing {false};
    std::string _error_kind;
    std::string _error_detail;
    std::string _installed_release;   // the release this run's install came from
    std::string _accepted_terms_version;   // read from terms_path at construction
    std::thread _thread;
};

}

#endif
