#include "r_utils/r_recorder_id.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_uuid.h"
#include <cctype>

using namespace std;
using namespace r_utils;

namespace
{
const char* ID_FILE_NAME = "recorder_id";

string read_id(const string& p)
{
    try
    {
        if(!r_fs::file_exists(p))
            return string();
        auto bytes = r_fs::read_file(p);
        auto s = r_string_utils::strip(string(bytes.begin(), bytes.end()));
        return r_recorder_id::well_formed(s) ? s : string();
    }
    catch(...)
    {
        return string();
    }
}
}

string r_utils::r_recorder_id::path(const string& top_dir)
{
    return top_dir + PATH_SLASH + ID_FILE_NAME;
}

bool r_utils::r_recorder_id::well_formed(const string& s)
{
    if(s.size() != 36)
        return false;
    for(size_t i = 0; i < s.size(); ++i)
    {
        if(i == 8 || i == 13 || i == 18 || i == 23)
        {
            if(s[i] != '-')
                return false;
        }
        else if(!isxdigit((unsigned char)s[i]) || isupper((unsigned char)s[i]))
            return false;
    }
    return true;
}

string r_utils::r_recorder_id::get_or_create(const string& top_dir)
{
    try
    {
        if(top_dir.empty())
            return string();
        auto p = path(top_dir);
        auto existing = read_id(p);
        if(!existing.empty())
            return existing;

        auto id = r_string_utils::to_lower(r_uuid::generate());
        if(!well_formed(id))
            return string();
        r_fs::atomic_write_private_file((const uint8_t*)id.data(), id.size(), p);

        // What was kept, read back: if it didn't stick, there's no id rather than one that changes every run.
        return read_id(p) == id ? id : string();
    }
    catch(...)
    {
        return string();
    }
}
