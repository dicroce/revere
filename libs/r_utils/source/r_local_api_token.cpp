#include "r_utils/r_local_api_token.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_platform.h"

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
    try
    {
        if(r_platform::is_windows())
            return r_fs::local_app_data_dir() + PATH_SLASH + "revere" + PATH_SLASH + "revere";
        if(r_platform::is_macos())
            return r_fs::local_app_data_dir() + "/Revere/revere";
        return r_fs::home_dir() + "/Documents/revere/revere";
    }
    catch(...)
    {
        return string();
    }
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
