#ifndef _r_vss_r_plugin_catalog_h
#define _r_vss_r_plugin_catalog_h

#include "r_vss/r_plugin_installer.h"
#include "r_vss/r_plugin_manifest.h"
#include "r_vss/r_system_plugin_host.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace r_vss
{

// The plugin catalog: the list of plugins a release offers, which the Store page
// shows. Each release publishes one catalog per platform, signed by the same keys as
// a plugin manifest (r_plugin_manifest.h):
//   revere-v0.0.41-windows-plugins.json, and .json.sig
// (the platform part is plugin_asset_platform()'s). It's for one platform, arch and
// revere version, and the signature covers all of it:
//
//   {"format":1,"platform":"windows","arch":"x86_64","revere_version":"0.0.41","plugins":[
//     {"id":"revere_cloud","type":"plugin","name":"Revere Cloud","description":"...",
//      "source":"closed","terms_url":"https://...","terms_version":2,
//      "file":"revere_cloud_system_plugin.dll",
//      "plugin":"revere-v0.0.41-windows-cloud-plugin.dll",
//      "manifest":"revere-v0.0.41-windows-cloud-plugin.dll.json",
//      "signature":"revere-v0.0.41-windows-cloud-plugin.dll.json.sig"},
//     {"id":"homekit","type":"link","name":"HomeKit","description":"...","url":"https://..."}]}
//
// A "plugin" entry names the release assets it installs from (plain file names, in
// the catalog's own release), and the file it's installed as; its own manifest is
// checked on install as always. A "link" entry has a url and no assets: the UI opens
// it. "beta" and "terms_version" (a whole number from 1; absent means no terms) are
// optional. "source" is "open" or "closed".

enum class r_catalog_entry_type
{
    plugin,   // installed from a release
    link      // opens a url
};

enum class r_catalog_source
{
    open,
    closed
};

struct r_catalog_entry
{
    std::string id;            // 1 to 40 of a-z, 0-9 and _
    r_catalog_entry_type type {r_catalog_entry_type::plugin};
    std::string name;
    std::string description;
    r_catalog_source source {r_catalog_source::open};
    bool beta {false};
    std::string terms_url;     // empty if none
    uint32_t terms_version {0};   // 0: no terms to accept
    // type plugin:
    std::string file;          // the file it's installed as, no folder
    std::string plugin;        // release asset names
    std::string manifest;
    std::string signature;
    // type link:
    std::string url;           // https
};

struct r_plugin_catalog
{
    int format {0};            // 1
    std::string platform;
    std::string arch;
    std::string revere_version;
    std::vector<r_catalog_entry> plugins;

    // The entry with this id, or null.
    R_API const r_catalog_entry* find(const std::string& id) const;
};

// The catalog in json. Throws r_invalid_argument_exception if it isn't one: not json,
// a field missing or of the wrong type, an unknown format, type or source, an id that's
// not a plain id or is there twice, an asset or file name with a folder in it, a link
// that isn't an https url, or a terms url that isn't one.
R_API r_plugin_catalog parse_plugin_catalog(const std::string& json);

// Checks a catalog and its signature in memory, in this order: one of trusted_keys
// signed it (nothing in an unsigned catalog is parsed); it parses; it's for this
// platform and arch; it's for this revere_version, exactly. On ok, catalog is set.
// Anything else is bad_signature, malformed, wrong_platform or wrong_version.
R_API r_plugin_check_result check_plugin_catalog(const std::string& catalog_json,
                                                 const std::string& signature,
                                                 const std::string& revere_version,
                                                 const std::vector<std::string>& trusted_keys,
                                                 const std::string& platform,
                                                 const std::string& arch,
                                                 r_plugin_catalog& catalog);

// A release's catalog and its signature, for platform and arch:
// https://github.com/dicroce/revere/releases/download/v<version>/revere-v<version>-<platform>-plugins.json
// (plugin_url is empty).
R_API r_plugin_release_assets catalog_release_assets(const std::string& revere_version,
                                                     const std::string& platform,
                                                     const std::string& arch);

// The assets a plugin entry installs from, in release version's downloads. Throws
// r_invalid_argument_exception for a link entry.
R_API r_plugin_release_assets catalog_entry_assets(const std::string& release_version, const r_catalog_entry& entry);

// The newest release at or below base_version that has this platform's catalog and its
// signature, for a dev build; empty if none has. (choose_plugin_release() for the catalog.)
R_API std::string choose_catalog_release(const std::vector<r_published_release>& releases,
                                         const std::string& base_version,
                                         const std::string& platform,
                                         const std::string& arch);

struct r_catalog_fetch_result
{
    r_plugin_install status {r_plugin_install::download_failed};   // ok, not_available, download_failed or check_failed
    r_plugin_check check {r_plugin_check::unreadable};   // when check_failed
    std::string detail;
    std::string release_version;   // the release it came from, when ok
    r_plugin_catalog catalog;      // when ok
    bool ok() const { return status == r_plugin_install::ok; }
};

// Fetches the catalog of the release a build of revere_version offers plugins from,
// and checks it: a release build's own release; a dev build's, the newest at or below its
// base version that has one (fetching REVERE_RELEASES_API_URL first). The catalog must be
// for exactly that release. not_available if there's none (a 404, or no such release).
R_API r_catalog_fetch_result fetch_plugin_catalog(const std::string& revere_version,
                                                  const r_plugin_fetch& fetch,
                                                  const std::vector<std::string>& trusted_keys,
                                                  const std::string& platform,
                                                  const std::string& arch);

// The same for this build, from GitHub, with a short timeout.
R_API r_catalog_fetch_result fetch_plugin_catalog();

// Installs a plugin entry from release_version (a catalog's revere_version) into dir,
// as install_system_plugin() does for any plugin: its manifest and signature are checked
// before it's fetched, and it's written atomically as entry.file. Throws
// r_invalid_argument_exception for a link entry. On ok, release_version is set.
R_API r_plugin_install_result install_catalog_plugin(const r_catalog_entry& entry,
                                                     const std::string& release_version,
                                                     const std::string& dir,
                                                     const r_plugin_fetch& fetch,
                                                     const std::vector<std::string>& trusted_keys,
                                                     const std::string& platform,
                                                     const std::string& arch);

// The same for this build, from GitHub.
R_API r_plugin_install_result install_catalog_plugin(const r_catalog_entry& entry,
                                                     const std::string& release_version,
                                                     const std::string& dir);

// What the Store page shows for a plugin entry.
enum class r_plugin_state
{
    not_installed,
    installed,          // loaded
    needs_update,       // found but not loaded: no manifest, or built for another revere
    failed_check,       // found but not loaded: it failed its integrity check
    removal_pending     // the user chose to uninstall it; it goes at the next start
};

// "not_installed", "installed", ... for status JSON.
R_API const char* plugin_state_name(r_plugin_state state);

// The state of a plugin entry, from what the plugin host reports: loaded (get_loaded_plugins()),
// skipped (get_skipped_plugins()), and whether the uninstall marker sits beside user_dir/entry.file.
// A plugin is named by its file's name without the extension. removal_pending wins; then
// installed; then needs_update or failed_check, with detail the skip's check; else not_installed.
R_API r_plugin_state plugin_entry_state(const r_catalog_entry& entry,
                                        const std::vector<std::string>& loaded,
                                        const std::vector<r_skipped_system_plugin>& skipped,
                                        const std::string& user_dir,
                                        std::string& detail);

// The terms a user accepted for each plugin, kept in a json file in the data folder:
//   {"plugins":{"revere_cloud":{"terms_version":2,"accepted_at":"2026-11-24T17:30:00Z"}}}
// A missing or unreadable file means nothing was accepted.
class r_plugin_terms
{
public:
    R_API explicit r_plugin_terms(const std::string& path);

    // The terms version accepted for plugin id; 0 if none.
    R_API uint32_t accepted_version(const std::string& id) const;

    // Whether installing entry must wait for the user to accept its terms: it has a
    // terms_version, and the accepted one is lower. (A higher terms_version asks again.)
    R_API bool needs_acceptance(const r_catalog_entry& entry) const;

    // Records that terms_version was accepted for plugin id now, and writes the file.
    // False, with why, if it can't be written; nothing is recorded then. A version lower
    // than the one already accepted is kept out.
    R_API bool accept(const std::string& id, uint32_t terms_version, std::string& why);

private:
    std::string _path;
    mutable std::mutex _lock;
    struct accepted { std::string id; uint32_t version; std::string at; };
    std::vector<accepted> _accepted;
};

}

#endif
