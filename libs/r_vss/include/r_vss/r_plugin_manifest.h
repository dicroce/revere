#ifndef _r_vss_r_plugin_manifest_h
#define _r_vss_r_plugin_manifest_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <string>
#include <vector>

namespace r_vss
{

// A system plugin's manifest, and the checks a release build makes before it
// loads the plugin (project-planning docs/IN_APP_PLUGIN_INSTALL.md, section 2).
//
// Beside the plugin file sit its manifest (<plugin file>.json) and the
// manifest's signature (<plugin file>.json.sig). The release CI writes the
// manifest and signs it with ECDSA P-256 over SHA-256 (DER, as
// `openssl dgst -sha256 -sign` writes it). The manifest names the plugin file
// and carries its size and sha256, so the one signature covers both.

struct r_plugin_manifest
{
    int format {0};               // 1
    std::string platform;         // "windows", "macos" or "linux" (plugin_platform())
    std::string arch;             // "x86_64" or "arm64" (plugin_arch())
    std::string revere_version;   // the exact revere version it was built for, "0.0.41"
    std::string file;             // the plugin's file name, no folder
    uint64_t size {0};            // the plugin file's size in bytes
    std::string sha256;           // the plugin file's sha256, 64 lowercase hex digits
};

// The manifest in json. Throws r_invalid_argument_exception if it isn't one:
// not json, a field missing or of the wrong type, an unknown format, an empty
// file name or one with a folder in it, or a sha256 that isn't 64 lowercase hex digits.
R_API r_plugin_manifest parse_plugin_manifest(const std::string& json);

// What a plugin's check found. Anything but ok means: don't load it.
enum class r_plugin_check
{
    ok,
    no_manifest,       // no <plugin>.json: from an older installer, or never signed; "needs updating"
    bad_signature,     // no signature, or no trusted key signed this manifest
    malformed,         // signed, but not a manifest this revere understands
    wrong_file,        // the manifest names another file
    wrong_platform,    // built for another platform or architecture
    wrong_version,     // built for another revere version; "needs updating"
    size_mismatch,     // the plugin file isn't the size the manifest says
    hash_mismatch,     // the plugin file isn't the one the manifest says
    unreadable         // a file couldn't be read
};

struct r_plugin_check_result
{
    r_plugin_check status {r_plugin_check::unreadable};
    std::string detail;   // what failed, for the log; empty when ok
    bool ok() const { return status == r_plugin_check::ok; }
};

// "ok", "no_manifest", "bad_signature", ... for logs and status JSON.
R_API const char* plugin_check_name(r_plugin_check status);

// Where a plugin's manifest and signature sit: <plugin_path>.json and <plugin_path>.json.sig.
R_API std::string plugin_manifest_path(const std::string& plugin_path);
R_API std::string plugin_signature_path(const std::string& plugin_path);

// This build's platform and architecture, as a manifest names them.
R_API std::string plugin_platform();
R_API std::string plugin_arch();

// The public keys (PEM) a release build trusts to sign plugin manifests, normally one.
// To rotate, add the new key in one release and sign with it from the next.
R_API const std::vector<std::string>& trusted_plugin_keys();

// Checks a manifest and its signature in memory (one just downloaded), in this
// order: one of trusted_keys signed it; it parses; it names file_name; it's for this
// platform and arch; it's for this revere_version, exactly. On ok, manifest is set.
R_API r_plugin_check_result check_plugin_manifest(const std::string& manifest_json,
                                                  const std::string& signature,
                                                  const std::string& file_name,
                                                  const std::string& revere_version,
                                                  const std::vector<std::string>& trusted_keys,
                                                  const std::string& platform,
                                                  const std::string& arch,
                                                  r_plugin_manifest& manifest);

// Checks the plugin at plugin_path, in this order, stopping at the first that fails:
// its manifest is there; its signature is there and one of trusted_keys made it;
// the manifest parses; it names this file; it's for this platform and arch; it's for
// this revere_version, exactly; the file's size and sha256 match it.
R_API r_plugin_check_result check_system_plugin(const std::string& plugin_path,
                                                const std::string& revere_version,
                                                const std::vector<std::string>& trusted_keys,
                                                const std::string& platform,
                                                const std::string& arch);

// The same, for this build: revere_version(), trusted_plugin_keys(), plugin_platform() and plugin_arch().
R_API r_plugin_check_result check_system_plugin(const std::string& plugin_path);

}

#endif
