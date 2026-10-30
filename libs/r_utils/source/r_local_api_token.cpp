#include "r_utils/r_local_api_token.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"

#ifdef IS_WINDOWS
#include <Windows.h>
#include <ShlObj.h>
#include <combaseapi.h>
#else
#include <pwd.h>
#include <unistd.h>
#include <vector>
#endif

using namespace std;
using namespace r_utils;

namespace
{
const char* TOKEN_FILE_NAME = "local_api_token";
}

string r_utils::r_local_api_token::path(const string& top_dir)
{
    return top_dir + PATH_SLASH + TOKEN_FILE_NAME;
}

void r_utils::r_local_api_token::write(const string& top_dir, const string& token)
{
    r_fs::atomic_write_private_file((const uint8_t*)token.data(), token.size(), path(top_dir));
}

r_nullable<string> r_utils::r_local_api_token::read(const string& top_dir)
{
    r_nullable<string> out;
    try
    {
        if(top_dir.empty())
            return out;
        auto p = path(top_dir);
        if(!r_fs::file_exists(p))
            return out;
        auto bytes = r_fs::read_file(p);
        auto token = r_string_utils::strip(string(bytes.begin(), bytes.end()));
        if(!token.empty())
            out.set_value(token);
    }
    catch(...)
    {
    }
    return out;
}

void r_utils::r_local_api_token::remove(const string& top_dir)
{
    try
    {
        auto p = path(top_dir);
        if(r_fs::file_exists(p))
            r_fs::remove_file(p);
    }
    catch(...)
    {
    }
}

string r_utils::r_local_api_token::default_top_dir()
{
#ifdef IS_WINDOWS
    wchar_t* local_app_data = nullptr;
    if(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &local_app_data) != S_OK)
    {
        if(local_app_data)
            CoTaskMemFree(local_app_data);
        return string();
    }
    wstring wide = wstring(local_app_data) + L"\\revere\\revere";
    CoTaskMemFree(local_app_data);
    return r_string_utils::convert_wide_string_to_multi_byte_string(wide.c_str());
#else
    auto res = sysconf(_SC_GETPW_R_SIZE_MAX);
    vector<char> buffer(res > 0 ? (size_t)res : (size_t)16384);
    passwd pwd;
    passwd* result = nullptr;
    if(getpwuid_r(getuid(), &pwd, buffer.data(), buffer.size(), &result) != 0 || !result || !pwd.pw_dir)
        return string();
    string home = pwd.pw_dir;
#ifdef IS_MACOS
    return home + "/Library/Application Support/Revere/revere";
#else
    return home + "/Documents/revere/revere";
#endif
#endif
}

bool r_utils::r_local_api_token::is_loopback_host(const string& host)
{
    auto h = r_string_utils::to_lower(r_string_utils::strip(host));
    if(h.size() >= 2 && h.front() == '[' && h.back() == ']')
        h = h.substr(1, h.size() - 2);
    return h == "localhost" || h == "::1" || h.rfind("127.", 0) == 0;
}

string r_utils::r_local_api_token::bearer(const string& token)
{
    return "Bearer " + token;
}
