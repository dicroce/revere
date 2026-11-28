#include "r_vss/r_plugin_action_request.h"
#include "r_vss/r_web_password.h"
#include "r_http/r_http_exception.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>

using namespace r_vss;
using namespace r_http;
using namespace std;
using json = nlohmann::json;

namespace
{

r_server_response _reply(const json& j, status_code code = response_ok)
{
    r_server_response response;
    if(code != response_ok)
        response.set_status_code(code);
    response.set_content_type("text/json");
    response.set_body(j.dump());
    return response;
}

r_server_response _error(status_code code, const string& message)
{
    return _reply(json{{"error", message}}, code);
}

}

vector<string> r_vss::plugin_offered_actions(const string& status_message)
{
    vector<string> out;
    auto j = json::parse(status_message, nullptr, false);
    if(!j.is_object() || !j.contains("lines") || !j["lines"].is_array())
        return out;
    auto str = [](const json& o, const char* key) {
        return o.is_object() && o.contains(key) && o[key].is_string() ? o[key].get<string>() : string();
    };
    for(const auto& line : j["lines"])
    {
        if(!line.is_object())
            continue;
        auto action = str(line, "action");
        if(!action.empty())
            out.push_back(action);
        if(line.contains("choice") && line["choice"].is_object())
        {
            const auto& c = line["choice"];
            auto prefix = str(c, "action");
            if(prefix.empty() || !c.contains("options") || !c["options"].is_array())
                continue;
            for(const auto& o : c["options"])
            {
                auto value = str(o, "value");
                if(!value.empty())
                    out.push_back(prefix + value);
            }
        }
    }
    return out;
}

r_server_response r_vss::post_system_plugin_action(const r_server_request& request, r_ws_auth& auth, const plugin_action_host& host)
{
    auto token = r_web_password::request_token(request);
    if(token.is_null() || !auth.valid(token.value()))
        R_STHROW(r_http_401_exception, ("Unauthorized."));

    auto req = json::parse(request.get_body_as_string(), nullptr, false);
    if(!req.is_object() || !req.contains("name") || !req["name"].is_string() || !req.contains("action") || !req["action"].is_string())
        return _error(response_bad_request, "Expected {\"name\": ..., \"action\": ...}.");

    auto name = req["name"].get<string>();
    auto action = req["action"].get<string>();

    auto loaded = host.loaded();
    if(find(loaded.begin(), loaded.end(), name) == loaded.end())
        return _error(response_not_found, "No plugin named " + name + ".");

    auto offered = plugin_offered_actions(host.status_message(name));
    if(action.empty() || find(offered.begin(), offered.end(), action) == offered.end())
        return _error(response_bad_request, "That plugin isn't offering that action.");

    if(!host.run(name, action))
        return _error(response_bad_request, "That plugin can't run actions.");

    return _reply(json{{"name", name}, {"action", action}});
}
