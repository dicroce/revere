
#include "utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_file.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/3rdparty/json/json.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"

#ifdef IS_WINDOWS
#include <shlobj_core.h>
#endif

#ifdef IS_LINUX
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

#include <deque>
#include <numeric>
#include <iterator>

using namespace std;
using namespace std::chrono;
using namespace r_utils;
using namespace r_http;
using namespace vision;
using json = nlohmann::json;

string vision::top_dir()
{
#ifdef IS_WINDOWS

    wchar_t* localAppData = nullptr;
    auto ret = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData);
    if(ret != S_OK)
        R_THROW(("Could not get path to LocalAppData folder."));

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
}

string vision::sub_dir(const string& subdir)
{
    auto top = top_dir();
    auto sd = top + PATH_SLASH + subdir + PATH_SLASH;

    if(!r_fs::file_exists(sd))
        r_fs::mkdir(sd);

    return sd;
}

string vision::join_path(const string& path, const string& fileName)
{
    return path + PATH_SLASH + fileName;
}
