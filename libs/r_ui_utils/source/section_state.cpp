
#include "r_ui_utils/section_state.h"

using namespace std;
using json = nlohmann::json;

r_ui_utils::sections_open r_ui_utils::parse_sections_open(const json& j)
{
    sections_open open;
    if(!j.is_object())
        return open;
    for(auto& [key, value] : j.items())
    {
        if(!key.empty() && value.is_boolean())
            open[key] = value.get<bool>();
    }
    return open;
}

json r_ui_utils::sections_open_to_json(const sections_open& open)
{
    auto j = json::object();
    for(const auto& [key, is_open] : open)
        j[key] = is_open;
    return j;
}

bool r_ui_utils::section_open(const sections_open& open, const string& key, bool def)
{
    auto found = open.find(key);
    return found == open.end() ? def : found->second;
}
