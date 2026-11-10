#include "cloud_download.h"
#include <cctype>

using namespace std;

revere::cloud_platform revere::this_cloud_platform()
{
#if defined(IS_WINDOWS)
    return cloud_platform::windows;
#elif defined(IS_LINUX) && defined(__aarch64__)
    return cloud_platform::linux_arm64;
#elif defined(IS_LINUX)
    return cloud_platform::linux_x86_64;
#elif defined(IS_MACOS) && defined(__x86_64__)
    return cloud_platform::macos_x86_64;
#elif defined(IS_MACOS)
    return cloud_platform::macos_arm64;
#else
    #error "Unsupported platform"
#endif
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
