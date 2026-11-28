#include "r_vss/r_plugin_states.h"
#include "r_utils/3rdparty/json/json.h"

using namespace r_vss;
using namespace std;
using njson = nlohmann::json;

int r_plugin_states::set(const string& guid, const string& state_json)
{
    if(guid.empty() || state_json.size() > MAX_STATE_BYTES)
        return -1;
    string normalized;
    try
    {
        auto parsed = njson::parse(state_json);
        if(!parsed.is_object())
            return -1;
        // Written back the same way every time, so the same JSON in another order or spacing is equal.
        normalized = parsed.dump();
    }
    catch(const exception&)
    {
        return -1;
    }

    lock_guard<mutex> g(_lock);
    auto it = _states.find(guid);
    if(it != _states.end() && it->second == normalized)
        return 0;
    _states[guid] = std::move(normalized);
    ++_version;
    return 0;
}

void r_plugin_states::remove(const string& guid)
{
    lock_guard<mutex> g(_lock);
    if(_states.erase(guid) > 0)
        ++_version;
}

string r_plugin_states::json() const
{
    lock_guard<mutex> g(_lock);
    njson plugins = njson::object();
    for(const auto& s : _states)
        plugins[s.first] = njson::parse(s.second);
    njson j;
    j["version"] = _version;
    j["plugins"] = plugins;
    return j.dump();
}

int64_t r_plugin_states::version() const
{
    lock_guard<mutex> g(_lock);
    return _version;
}
