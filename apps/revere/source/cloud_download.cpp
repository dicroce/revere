#include "cloud_download.h"
#include "r_utils/r_platform.h"
#include <cctype>

using namespace std;

revere::cloud_platform revere::this_cloud_platform()
{
    const string arch = r_utils::r_platform::arch_name();
    if(r_utils::r_platform::is_windows())
        return cloud_platform::windows;
    if(r_utils::r_platform::is_linux())
        return (arch == "arm64") ? cloud_platform::linux_arm64 : cloud_platform::linux_x86_64;
    return (arch == "x86_64") ? cloud_platform::macos_x86_64 : cloud_platform::macos_arm64;
}

static bool _is_plain_version(const string& v)
{
    int parts = 0;
    size_t i = 0;
    while(i < v.size())
    {
        size_t start = i;
        while(i < v.size() && isdigit((unsigned char)v[i]))
            ++i;
        if(i == start)
            return false;
        ++parts;
        if(i == v.size())
            break;
        if(v[i] != '.' || parts == 3)
            return false;
        ++i;
        if(i == v.size())
            return false;
    }
    return parts == 3;
}

string revere::revere_cloud_download_url(const string& version, cloud_platform platform)
{
    if(!_is_plain_version(version))
        return "https://github.com/dicroce/revere/releases/latest";

    // The names build-windows, build-linux(-arm64) and build-macos give the assets.
    const char* asset = "";
    switch(platform)
    {
    case cloud_platform::windows:      asset = "-windows-cloud-setup.exe"; break;
    case cloud_platform::linux_arm64:  asset = "-linux-arm64-cloud.run"; break;
    case cloud_platform::linux_x86_64: asset = "-linux-cloud.run"; break;
    case cloud_platform::macos_x86_64: asset = "-macos-intel-cloud.command"; break;
    case cloud_platform::macos_arm64:  asset = "-macos-cloud.command"; break;
    }
    return "https://github.com/dicroce/revere/releases/download/v" + version + "/revere-v" + version + asset;
}

string revere::cloud_install_error_text(const string& error_kind)
{
    if(error_kind.empty())
        return string();
    if(error_kind == "not_available")
        return "Revere Cloud isn't available for this version of Revere yet. You can get it from the download page instead.";
    if(error_kind == "download_failed")
        return "Revere Cloud couldn't be downloaded. Check this computer's internet connection and try again.";
    if(error_kind == "check_failed")
        return "The downloaded Revere Cloud failed its integrity check, so it wasn't installed.";
    if(error_kind == "write_failed")
        return "Revere Cloud couldn't be saved to the plugins folder.";
    if(error_kind == "load_failed")
        return "Revere Cloud was installed but couldn't be started. Restart Revere to try again.";
    return "Revere Cloud couldn't be installed.";
}

string revere::cloud_skipped_label(const string& state)
{
    if(state == "needs_update")
        return "Needs updating";
    if(state == "failed_check")
        return "Failed check";
    return string();
}

string revere::cloud_skipped_text(const string& state)
{
    if(state == "needs_update")
        return "Revere Cloud needs updating for this version of Revere.";
    if(state == "failed_check")
        return "Revere Cloud failed its integrity check, so it wasn't started. Reinstall it.";
    return string();
}
