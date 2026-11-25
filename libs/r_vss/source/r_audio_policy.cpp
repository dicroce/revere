#include "r_vss/r_audio_policy.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/3rdparty/json/json.h"

using namespace r_vss;
using namespace r_utils;
using namespace std;

bool r_audio_policy::records_audio(const string& camera_id) const
{
    auto it = cameras.find(camera_id);
    return it == cameras.end() ? default_audio : it->second;
}

r_nullable<r_audio_policy> r_vss::parse_audio_policy(const string& json_text)
{
    r_nullable<r_audio_policy> result;
    auto j = nlohmann::json::parse(json_text, nullptr, false);
    if(!j.is_object() || !j.contains("default_audio") || !j["default_audio"].is_boolean())
        return result;
    r_audio_policy policy;
    policy.default_audio = j["default_audio"].get<bool>();
    if(j.contains("managed_by"))
    {
        if(!j["managed_by"].is_string() || (j["managed_by"] != "cloud" && j["managed_by"] != "local"))
            return result;
        policy.cloud_managed = j["managed_by"] == "cloud";
    }
    if(j.contains("cameras"))
    {
        if(!j["cameras"].is_object())
            return result;
        for(auto& [id, on] : j["cameras"].items())
        {
            if(id.empty() || !on.is_boolean())
                return result;
            policy.cameras[id] = on.get<bool>();
        }
    }
    result.set_value(policy);
    return result;
}

string r_vss::audio_policy_json(const r_audio_policy& policy)
{
    nlohmann::json cameras = nlohmann::json::object();
    for(auto& [id, on] : policy.cameras)
        cameras[id] = on;
    return nlohmann::json{{"default_audio", policy.default_audio}, {"cameras", cameras},
                          {"managed_by", policy.cloud_managed ? "cloud" : "local"}}.dump();
}

static string _policy_path(const string& top_dir)
{
    return top_dir + PATH_SLASH + "config" + PATH_SLASH + "audio.json";
}

r_audio_policy r_vss::load_audio_policy(const string& top_dir)
{
    auto path = _policy_path(top_dir);
    try
    {
        if(!r_fs::file_exists(path))
            return r_audio_policy();
        auto buf = r_fs::read_file(path);
        auto policy = parse_audio_policy(string((char*)buf.data(), buf.size()));
        if(!policy.is_null())
            return policy.value();
        R_LOG_WARNING("Audio: %s isn't a policy, so every camera's audio is recorded until the cloud sends one", path.c_str());
    }
    catch(const exception& ex)
    {
        R_LOG_WARNING("Audio: couldn't read %s, so every camera's audio is recorded until the cloud sends one: %s", path.c_str(), ex.what());
    }
    return r_audio_policy();
}

bool r_vss::save_audio_policy(const string& top_dir, const r_audio_policy& policy)
{
    auto path = _policy_path(top_dir);
    auto txt = audio_policy_json(policy);
    try
    {
        r_fs::mkdir_p(top_dir + PATH_SLASH + "config");
        r_fs::atomic_write_file((const uint8_t*)txt.c_str(), txt.size(), path);
        return true;
    }
    catch(const exception& ex)
    {
        R_LOG_ERROR("Audio: couldn't save %s: %s", path.c_str(), ex.what());
        return false;
    }
}

bool r_vss::audio_policy_set_allowed(const r_audio_policy& current, bool from_local_token)
{
    return from_local_token || !current.cloud_managed;
}

r_audio_policy r_vss::audio_policy_as_set(r_audio_policy requested, bool from_local_token)
{
    requested.cloud_managed = from_local_token;
    return requested;
}

r_audio_policy r_vss::audio_policy_released(r_audio_policy policy)
{
    policy.cloud_managed = false;
    return policy;
}

r_audio_policy r_vss::audio_policy_at_startup(const r_audio_policy& saved, bool cloud_plugin_running)
{
    return cloud_plugin_running ? saved : audio_policy_released(saved);
}

bool r_vss::is_cloud_plugin_name(const string& name)
{
    return name.rfind("revere_cloud", 0) == 0;
}

set<string> r_vss::audio_policy_restarts(const map<string, bool>& running, const r_audio_policy& policy)
{
    set<string> restarts;
    for(auto& [id, records] : running)
        if(records != policy.records_audio(id))
            restarts.insert(id);
    return restarts;
}
