
#include "utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"

#ifdef IS_WINDOWS
#include <shlobj_core.h>
#include <shellapi.h>
#endif

#if defined(IS_LINUX) || defined(IS_MACOS)
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

#include <deque>
#include <numeric>
#include <iterator>

using namespace std;
using namespace r_utils;

string revere::top_dir()
{
#ifdef IS_WINDOWS

    wchar_t* localAppData = nullptr;
    auto ret = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData);
    if(ret != S_OK)
        R_THROW(("Could not get path to documents folder."));

    std::wstring wide_revere_path = std::wstring(localAppData) + L"\\revere";

    auto revere_path = r_string_utils::convert_wide_string_to_multi_byte_string(wide_revere_path.c_str());

    if(!r_fs::file_exists(revere_path))
        r_fs::mkdir(revere_path);

    revere_path += PATH_SLASH + "revere";

    if(!r_fs::file_exists(revere_path))
        r_fs::mkdir(revere_path);

    CoTaskMemFree(localAppData);

    return revere_path;
#endif

#ifdef IS_LINUX
    auto res = sysconf(_SC_GETPW_R_SIZE_MAX);
    if(res == -1)
        R_THROW(("Could not get path to documents folder."));
    vector<char> buffer(res);
    passwd pwd;
    passwd* result;
    auto ret = getpwuid_r(getuid(), &pwd, buffer.data(), buffer.size(), &result);
    if(ret != 0)
        R_THROW(("Could not get path to documents folder."));
    string path = string(pwd.pw_dir) + PATH_SLASH + "Documents" + PATH_SLASH + "revere";
    if(!r_fs::file_exists(path))
        r_fs::mkdir(path);
    path += PATH_SLASH + "revere";
    if(!r_fs::file_exists(path))
        r_fs::mkdir(path);
    return path;
#endif

#ifdef IS_MACOS
    auto res = sysconf(_SC_GETPW_R_SIZE_MAX);
    if(res == -1)
        R_THROW(("Could not get path to application support folder."));
    vector<char> buffer(res);
    passwd pwd;
    passwd* result;
    auto ret = getpwuid_r(getuid(), &pwd, buffer.data(), buffer.size(), &result);
    if(ret != 0)
        R_THROW(("Could not get path to application support folder."));
    string path = string(pwd.pw_dir) + PATH_SLASH + "Library" + PATH_SLASH + "Application Support" + PATH_SLASH + "Revere";
    if(!r_fs::file_exists(path))
        r_fs::mkdir(path);
    path += PATH_SLASH + "revere";
    if(!r_fs::file_exists(path))
        r_fs::mkdir(path);
    return path;
#endif
}

string revere::sub_dir(const string& subdir)
{
    auto top = top_dir();
    auto sd = top + PATH_SLASH + subdir + PATH_SLASH;

    if(!r_fs::file_exists(sd))
        r_fs::mkdir(sd);

    return sd;
}

std::string revere::join_path(const std::string& path, const std::string& fileName)
{
    return path + PATH_SLASH + fileName;
}

bool revere::open_url_in_browser(const std::string& url)
{
#ifdef IS_WINDOWS
    // Windows: Use ShellExecute
    R_LOG_INFO("Opening URL in browser: %s", url.c_str());
    HINSTANCE result = ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
    if(result <= (HINSTANCE)32)
    {
        R_LOG_ERROR("ShellExecuteA failed to open URL: %s (error code: %lld)", url.c_str(), (long long)(INT_PTR)result);
        R_THROW(("Failed to open URL in browser. Error code: %d (%s)", (INT_PTR)result, r_utils::last_error_to_string().c_str()));
    }
    return true;
#elif defined(IS_MACOS)
    // macOS: Use open command
    std::string command = "open \"" + url + "\"";
    R_LOG_INFO("Opening URL with command: %s", command.c_str());
    int result = system(command.c_str());
    if(result != 0)
    {
        R_LOG_ERROR("open command failed for URL: %s (exit code: %d)", url.c_str(), result);
        R_THROW(("Failed to open URL in browser. Error code: %d", result));
    }
#elif defined(IS_LINUX)
    // Linux: Use xdg-open which works with XDG Desktop Portal in snaps
    std::string command = "xdg-open \"" + url + "\"";
    R_LOG_INFO("Opening URL with command: %s", command.c_str());
    int result = system(command.c_str());
    if(result != 0)
    {
        R_LOG_ERROR("xdg-open failed for URL: %s (exit code: %d)", url.c_str(), result);
        R_THROW(("Failed to open URL in browser. Error code: %d", result));
    }
    R_LOG_INFO("xdg-open succeeded for URL: %s", url.c_str());
#else
    R_LOG_ERROR("open_url_in_browser: unsupported platform");
    return false;  // Unsupported platform
#endif
    return true;
}

