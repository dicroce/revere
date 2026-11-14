
#include "utils.h"
#include "browser_url.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_local_api_token.h"

#include "r_utils/r_desktop.h"

#include <deque>
#include <numeric>
#include <iterator>
#include <thread>

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

    // The folder Revere, Vision and revere_mcp share for this user.
    auto path = r_local_api_token::default_top_dir();
    if(path.empty())
        R_THROW(("Could not get the path to this user's data folder."));
    r_fs::mkdir_p(path);  // mkdir_p: another process may make it at the same moment
    return path;
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

bool revere::open_url_in_browser(const std::string& url)
{
    if(!is_web_url(url))
    {
        R_LOG_ERROR("Not opening a link that isn't an http(s) URL");
        return false;
    }
    R_LOG_INFO("Opening URL in browser: %s", url.c_str());
    r_desktop::open(url);
    return true;
}

