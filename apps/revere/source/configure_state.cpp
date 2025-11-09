
#include "configure_state.h"
#include "utils.h"
#include "r_utils/r_file.h"
#include "r_utils/3rdparty/json/json.h"

using namespace revere;
using namespace r_utils;
using namespace std;

using json = nlohmann::json;

void configure_state::load()
{
    lock_guard<mutex> lock(_mutex);
    auto cfg_path = sub_dir("config") + "revere_cfg.json";
    if(r_fs::file_exists(cfg_path))
    {
        auto cfg_buffer = r_fs::read_file(cfg_path);
        auto j = json::parse(string((char*)cfg_buffer.data(), cfg_buffer.size()));

        config_map.clear();
        for(auto& el : j.items())
        {
            config_map[el.key()] = el.value().get<string>();
        }
    }
}

bool configure_state::need_save()
{
    lock_guard<mutex> lock(_mutex);
    return _need_save;
}

void configure_state::save()
{
    lock_guard<mutex> lock(_mutex);
    json j;
    for(const auto& kvp : config_map)
    {
        j[kvp.first] = kvp.second;
    }

    auto cfg_txt = j.dump();
    r_fs::write_file((uint8_t*)cfg_txt.c_str(), cfg_txt.size(), sub_dir("config") + "revere_cfg.json");

    _need_save = false;
}

void configure_state::set_value(const string& key, const string& value)
{
    lock_guard<mutex> lock(_mutex);
    config_map[key] = value;
    _need_save = true;
}

string configure_state::get_value(const string& key, const string& default_value) const
{
    lock_guard<mutex> lock(_mutex);
    auto it = config_map.find(key);
    if(it != config_map.end())
        return it->second;
    return default_value;
}

bool configure_state::get_bool(const string& key, bool default_value) const
{
    lock_guard<mutex> lock(_mutex);
    auto it = config_map.find(key);
    if(it != config_map.end())
    {
        if(it->second == "true")
            return true;
        else if(it->second == "false")
            return false;
    }
    return default_value;
}

void configure_state::set_bool(const string& key, bool value)
{
    lock_guard<mutex> lock(_mutex);
    config_map[key] = value ? "true" : "false";
    _need_save = true;
}
