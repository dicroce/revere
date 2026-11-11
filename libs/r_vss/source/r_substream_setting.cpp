#include "r_vss/r_substream_setting.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/3rdparty/json/json.h"

using namespace r_vss;
using namespace r_utils;
using namespace std;
using json = nlohmann::json;

bool r_vss::parse_substream_setting(const string& json_text)
{
    auto j = json::parse(json_text, nullptr, false);
    return j.is_object() && j.contains("enabled") && j["enabled"].is_boolean() && j["enabled"].get<bool>();
}

string r_vss::substream_setting_json(bool enabled)
{
    return json{{"enabled", enabled}}.dump();
}

r_substream_setting::r_substream_setting(const string& top_dir) :
    _config_dir(top_dir + PATH_SLASH + "config"),
    _path(_config_dir + PATH_SLASH + "substreams.json")
{
    try
    {
        if(r_fs::file_exists(_path))
        {
            auto buf = r_fs::read_file(_path);
            _enabled = parse_substream_setting(string((char*)buf.data(), buf.size()));
        }
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("Smaller streams: couldn't read %s, they stay off: %s", _path.c_str(), ex.what());
    }
}

bool r_substream_setting::set_enabled(bool enabled)
{
    lock_guard<mutex> g(_write);
    if(_enabled.load() == enabled)
        return false;
    _enabled = enabled;
    auto txt = substream_setting_json(enabled);
    try
    {
        r_fs::mkdir_p(_config_dir);
        r_fs::atomic_write_file((const uint8_t*)txt.c_str(), txt.size(), _path);
    }
    catch(const exception& ex)
    {
        R_LOG_ERROR("Smaller streams: couldn't save %s: %s", _path.c_str(), ex.what());
    }
    R_LOG_INFO("Smaller streams: %s for this computer.", enabled ? "on" : "off");
    return true;
}
