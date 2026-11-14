#ifndef _r_vss_r_plugin_installer_h
#define _r_vss_r_plugin_installer_h

#include "r_vss/r_plugin_manifest.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace r_vss
{

// Downloading and installing a signed system plugin (project-planning
// docs/IN_APP_PLUGIN_INSTALL.md, sections 1, 2 and 4). Integrity rests on the
// manifest's signature and the plugin's sha256, not on TLS: r_ssl_socket falls
// back to no certificate check when it finds no CA bundle.

// An http(s) URL split for a request: target is the path and query, exactly as given.
struct r_http_url
{
    std::string scheme;   // "https" or "http"
    std::string host;
    int port {0};         // 443 or 80 unless the URL names one
    std::string target;   // "/a/b?c=d", "/" if the URL has none
};

// Throws r_invalid_argument_exception if url isn't an absolute http or https URL.
R_API r_http_url parse_http_url(const std::string& url);

// The URL a redirect's Location header points to, from the URL that answered:
// absolute, scheme-relative ("//host/x") or path-absolute ("/x"). Throws
// r_invalid_argument_exception for anything else.
R_API std::string resolve_redirect(const std::string& from_url, const std::string& location);

// GETs url's body, following up to 5 redirects (GitHub's release downloads redirect
// once, to a signed CDN URL). Never from https to http. Throws r_not_found_exception
// on a 404, and r_exception on any other failure, or a body bigger than max_bytes.
R_API std::vector<uint8_t> http_get(const std::string& url, size_t max_bytes, uint64_t timeout_ms = 60000);

// Fetches a URL's body, like http_get; tests give a fake one.
typedef std::function<std::vector<uint8_t>(const std::string& url, size_t max_bytes)> r_plugin_fetch;

// A release's Revere Cloud plugin: three assets beside its installers.
struct r_plugin_release_assets
{
    std::string plugin_url;
    std::string manifest_url;
    std::string signature_url;
};

// The file a Revere Cloud plugin is installed as: revere_cloud_system_plugin.dll,
// .dylib or .so. Its manifest names this file.
R_API std::string revere_cloud_plugin_file();

// dicroce/revere's release v<revere_version> assets for platform and arch (as
// plugin_platform() and plugin_arch() name them):
//   revere-v0.0.41-windows-cloud-plugin.dll, and .dll.json, .dll.json.sig
// The platform part is the installers': windows, macos, macos-intel, linux, linux-arm64.
R_API r_plugin_release_assets revere_cloud_release_assets(const std::string& revere_version,
                                                          const std::string& platform,
                                                          const std::string& arch);

enum class r_plugin_install
{
    ok,
    not_available,     // the release has no manifest for this platform (a 404): offer the download page
    download_failed,   // couldn't fetch an asset: no network, GitHub down, a timeout
    check_failed,      // fetched, but the signature, version, platform, size or hash is wrong
    write_failed       // couldn't write it into the plugin folder
};

struct r_plugin_install_result
{
    r_plugin_install status {r_plugin_install::download_failed};
    r_plugin_check check {r_plugin_check::unreadable};   // what the check found, when check_failed
    std::string detail;   // what failed, for the log and the UI; empty when ok
    std::string path;     // the installed plugin's path, when ok
    std::string release_version;   // the release it came from ("0.0.41"), when ok
    bool ok() const { return status == r_plugin_install::ok; }
};

// "ok", "not_available", ... for logs and status JSON.
R_API const char* plugin_install_name(r_plugin_install status);

// Downloads a plugin into dir as file_name, with its manifest and signature beside it:
//  1. fetches the manifest and signature, and checks them (check_plugin_manifest())
//     before fetching the plugin;
//  2. fetches the plugin, and checks its size and sha256 against the manifest;
//  3. writes the signature, the manifest and then the plugin, each to a temporary
//     file that is flushed to disk and renamed over the old one;
//  4. checks the installed plugin as the loader will (check_system_plugin()).
// Nothing is written unless steps 1 and 2 pass. The plugin at dir/file_name must not
// be loaded: Windows can't replace a loaded DLL, and replacing a running library's
// file elsewhere isn't safe. The caller loads it after (r_system_plugin_host::load_and_start).
R_API r_plugin_install_result install_system_plugin(const r_plugin_release_assets& assets,
                                                    const std::string& dir,
                                                    const std::string& file_name,
                                                    const r_plugin_fetch& fetch,
                                                    const std::string& revere_version,
                                                    const std::vector<std::string>& trusted_keys,
                                                    const std::string& platform,
                                                    const std::string& arch);

// Which release a build installs Revere Cloud from.
//
// A release build ("0.0.42") installs from its own release, and the manifest must be
// for exactly that version. A dev build ("0.0.42-dirty", "0.0.41-5-gabc1234") has no
// release of its own, so for testing it installs from the newest published release
// at or below its base version (the x.y.z it starts with) that has a signed plugin
// for this platform. The manifest must then be for exactly that release. The
// signature, platform, size and hash checks are the same for both.

// The x.y.z a version starts with, when it's followed by nothing or by "-":
// "0.0.42-dirty" and "0.0.42" give "0.0.42", "0.0.41-5-gabc1234" gives "0.0.41".
// Empty for anything else ("unknown", "abc1234", "0.0", "v0.0.41").
R_API std::string release_base_version(const std::string& version);

// <0, 0 or >0 as release version a (x.y.z) is older than, the same as or newer than b.
// Throws r_invalid_argument_exception if either isn't x.y.z.
R_API int compare_release_versions(const std::string& a, const std::string& b);

// A published release, from GitHub's list of releases.
struct r_published_release
{
    std::string version;               // "0.0.41", from the tag "v0.0.41"
    std::vector<std::string> assets;   // its asset file names
};

// The releases in GitHub's releases JSON (GET /repos/{owner}/{repo}/releases), in its
// order. Drafts, prereleases and tags that aren't vx.y.z are left out. Throws
// r_invalid_argument_exception if it isn't a JSON array.
R_API std::vector<r_published_release> parse_published_releases(const std::string& json);

// The newest release at or below base_version that has this platform's Revere Cloud
// plugin, its manifest and its signature; empty if none has.
R_API std::string choose_plugin_release(const std::vector<r_published_release>& releases,
                                        const std::string& base_version,
                                        const std::string& platform,
                                        const std::string& arch);

// dicroce/revere's newest releases, for a dev build to choose from.
constexpr const char* REVERE_RELEASES_API_URL = "https://api.github.com/repos/dicroce/revere/releases?per_page=30";

// Installs Revere Cloud for a build of revere_version into dir as file_name, from the
// release chosen as above. A release build fetches nothing else; a dev build first
// fetches REVERE_RELEASES_API_URL. not_available if a dev build's version has no base,
// or no release has a plugin for it. On ok, release_version is the release used.
R_API r_plugin_install_result install_revere_cloud_plugin(const std::string& dir,
                                                          const std::string& revere_version,
                                                          const std::string& file_name,
                                                          const r_plugin_fetch& fetch,
                                                          const std::vector<std::string>& trusted_keys,
                                                          const std::string& platform,
                                                          const std::string& arch);

// The same for this build, from GitHub: revere_version(), revere_cloud_plugin_file(),
// http_get, trusted_plugin_keys(), plugin_platform() and plugin_arch().
R_API r_plugin_install_result install_revere_cloud_plugin(const std::string& dir);

}

#endif
