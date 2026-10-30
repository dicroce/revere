
#include "utils.h"
#include "browser_url.h"
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
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>
extern char** environ;
#endif

#include <deque>
#include <numeric>
#include <iterator>

using namespace std;
using namespace r_utils;

static string g_data_dir_override;

void revere::set_data_dir(const string& dir)
{
    g_data_dir_override = dir;
}

string revere::top_dir()
{
    // Explicit override (--data-dir) wins and bypasses the home/passwd lookup
    // below — important for daemon/service users that may have no usable home.
    if(!g_data_dir_override.empty())
    {
        if(!r_fs::file_exists(g_data_dir_override))
            r_fs::mkdir_p(g_data_dir_override);
        return g_data_dir_override;
    }

#ifdef IS_WINDOWS

    wchar_t* localAppData = nullptr;
    auto ret = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData);
    if(ret != S_OK)
        R_THROW(("Could not get path to documents folder."));

    std::wstring wide_revere_path = std::wstring(localAppData) + L"\\revere";

    auto revere_path = r_string_utils::convert_wide_string_to_multi_byte_string(wide_revere_path.c_str());

    r_fs::mkdir_p(revere_path);  // mkdir_p: another process may make it at the same moment

    revere_path += PATH_SLASH + "revere";

    r_fs::mkdir_p(revere_path);  // mkdir_p: another process may make it at the same moment

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
    // mkdir_p (not mkdir): a fresh/headless Linux account may not have ~/Documents
    // yet; single-level mkdir would throw on the missing parent and abort startup.
    if(!r_fs::file_exists(path))
        r_fs::mkdir_p(path);
    path += PATH_SLASH + "revere";
    if(!r_fs::file_exists(path))
        r_fs::mkdir_p(path);
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
    r_fs::mkdir_p(path);  // mkdir_p: another process may make it at the same moment
    path += PATH_SLASH + "revere";
    r_fs::mkdir_p(path);  // mkdir_p: another process may make it at the same moment
    return path;
#endif
}

string revere::sub_dir(const string& subdir)
{
    auto top = top_dir();
    auto sd = top + PATH_SLASH + subdir + PATH_SLASH;

    // mkdir_p, not mkdir: two processes starting at once (two View windows, say)
    // both see it missing, and the second mkdir would throw on the first's dir.
    r_fs::mkdir_p(sd);

    return sd;
}

std::string revere::join_path(const std::string& path, const std::string& fileName)
{
    return path + PATH_SLASH + fileName;
}

#if defined(IS_MACOS) || defined(IS_LINUX)
// Runs the opener with the URL as its one argument, never through a shell, and
// waits for it, as system() did.
static int _run_opener(const char* opener, const std::string& url)
{
    char* argv[] = {const_cast<char*>(opener), const_cast<char*>(url.c_str()), nullptr};
    pid_t pid;
    int err = posix_spawnp(&pid, opener, nullptr, nullptr, argv, environ);
    if(err != 0)
        return -1;
    int status = 0;
    while(waitpid(pid, &status, 0) < 0)
    {
        if(errno != EINTR)
            return -1;
    }
    return (WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
}
#endif

bool revere::open_url_in_browser(const std::string& url)
{
    if(!is_web_url(url))
    {
        R_LOG_ERROR("Not opening a link that isn't an http(s) URL");
        return false;
    }
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
    R_LOG_INFO("Opening URL with open: %s", url.c_str());
    int result = _run_opener("open", url);
    if(result != 0)
    {
        R_LOG_ERROR("open command failed for URL: %s (exit code: %d)", url.c_str(), result);
        R_THROW(("Failed to open URL in browser. Error code: %d", result));
    }
#elif defined(IS_LINUX)
    // Linux: Use xdg-open which works with XDG Desktop Portal in snaps
    R_LOG_INFO("Opening URL with xdg-open: %s", url.c_str());
    int result = _run_opener("xdg-open", url);
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

