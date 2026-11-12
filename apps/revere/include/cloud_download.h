#ifndef __revere_cloud_download_h
#define __revere_cloud_download_h

#include <string>

namespace revere
{

// The release assets' platforms: each has its own Revere Cloud installer.
enum class cloud_platform { windows, linux_x86_64, linux_arm64, macos_arm64, macos_x86_64 };

// The platform this build is for.
cloud_platform this_cloud_platform();

// Where "Download" in About Revere Cloud sends the browser. A build made on a
// release tag has a plain "x.y.z" version and its installer is a release asset,
// so it links straight to that file, the one for `platform`. Any other build (a
// commit hash, "0.0.30-4-gabc1234", "unknown") has no release of its own, so it
// opens the latest release instead of a 404.
std::string revere_cloud_download_url(const std::string& version, cloud_platform platform);

// What the user is told when an in-app install fails, by r_cloud_install_status's
// error_kind ("not_available", "download_failed", "check_failed", "write_failed",
// "load_failed"); empty for "". The web UI's PluginsSection.vue says the same.
std::string cloud_install_error_text(const std::string& error_kind);

// The Plugins row for a Revere Cloud plugin that's there but not loaded, by
// r_cloud_install_status's state ("needs_update" or "failed_check"): its status
// label and the line under it. Empty for any other state.
std::string cloud_skipped_label(const std::string& state);
std::string cloud_skipped_text(const std::string& state);

}

#endif
