
#include "configure_state.h"
#include "utils.h"
#include "r_utils/r_file.h"
#include "r_utils/3rdparty/json/json.h"

using namespace vision;
using namespace r_utils;
using namespace std;

using json = nlohmann::json;

void configure_state::load()
{
    auto cfg_path = sub_dir("config") + "vision_cfg.json";
    if(r_fs::file_exists(cfg_path))
    {    
        auto cfg_buffer = r_fs::read_file(cfg_path);
        auto j = json::parse(string((char*)cfg_buffer.data(), cfg_buffer.size()));
        if(j.contains("revere_ipv4"))
            revere_ipv4.set_value(j["revere_ipv4"].get<string>());
        if(j.contains("current_layout"))
            current_layout = s_to_layout(j["current_layout"].get<string>());
        stream_map.clear();
        if(j.contains("stream_map"))
        {
            for(auto smo : j["stream_map"])
            {
                stream_info si;
                si.name = smo["name"].get<string>();
                si.camera_id = smo["camera_id"].get<string>();
                si.rtsp_url = smo["rtsp_url"].get<string>();
                si.do_motion_detection = smo["do_motion_detection"].get<bool>();
                stream_map.insert(make_pair(si.name, si));
            }
        }
    }
}

bool configure_state::need_save()
{
    return _need_save;
}

void configure_state::save()
{
    json j;
    if(!revere_ipv4.is_null())
        j["revere_ipv4"] = revere_ipv4.value();

    j["current_layout"] = layout_to_s(current_layout);

    auto sm = json::array();
    for(auto smo : stream_map)
    {
        json smo_j;
        smo_j["name"] = smo.second.name;
        smo_j["rtsp_url"] = smo.second.rtsp_url;
        smo_j["camera_id"] = smo.second.camera_id;
        smo_j["do_motion_detection"] = smo.second.do_motion_detection;
        sm.push_back(smo_j);
    }
    j["stream_map"] = sm;

    auto cfg_txt = j.dump();
    r_fs::write_file((uint8_t*)cfg_txt.c_str(), cfg_txt.size(), sub_dir("config") + "vision_cfg.json");

    _need_save = false;
}

void configure_state::set_revere_ipv4(const string& ip)
{
    revere_ipv4 = ip;
    _need_save = true;
}

r_nullable<string> configure_state::get_revere_ipv4() const
{
    return revere_ipv4;
}

void configure_state::set_current_layout(layout l)
{
    current_layout = l;
    _need_save = true;
}

layout configure_state::get_current_layout() const
{
    return current_layout;
}

string configure_state::make_name(int window, layout l, int index) const
{
    return r_string_utils::format("%d_%s_%d", window, layout_to_s(l).c_str(), index);
}

void configure_state::set_stream_info(const stream_info& si)
{
    stream_map[si.name] = si;
    _need_save = true;
}

void configure_state::unset_stream_info(const std::string& name)
{
    stream_map.erase(name);
    _need_save = true;
}

r_nullable<stream_info> configure_state::get_stream_info(const string& name) const
{
    r_nullable<stream_info> output;

    if(stream_map.count(name) > 0)
        output.set_value(stream_map.at(name));

    return output;
}

vector<stream_info> configure_state::collect_stream_info(int window, layout l) const
{
    auto key = r_string_utils::format("%d_%s", window, layout_to_s(l).c_str());
    map<string, stream_info>::const_iterator i = stream_map.lower_bound(key);
    vector<stream_info> output;
    while(i != cend(stream_map) && i->first.find(key) != string::npos)
    {
        output.push_back(i->second);
        ++i;
    }
    return output;
}