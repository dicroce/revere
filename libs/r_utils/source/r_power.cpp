#include "r_utils/r_power.h"

using namespace r_utils;
using namespace std;

string r_utils::power_source_name(r_power_source source)
{
    switch(source)
    {
        case r_power_source::ac: return "ac";
        case r_power_source::battery: return "battery";
        default: return "unknown";
    }
}

r_power_source r_utils::power_source_from_supplies(const vector<r_power_supply>& supplies)
{
    bool has_battery = false;
    for(const auto& s : supplies)
    {
        // A wireless mouse's battery says nothing about the computer's power.
        if(s.scope == "Device")
            continue;
        if(s.type == "Battery")
            has_battery = true;
        else if(s.online)
            return r_power_source::ac;
    }
    return has_battery ? r_power_source::battery : r_power_source::ac;
}

r_power_source r_utils::power_source_from_status(uint8_t ac_line_status, uint8_t battery_flag)
{
    if(ac_line_status == 1)
        return r_power_source::ac;
    if(battery_flag != 255 && (battery_flag & 128))
        return r_power_source::ac;
    if(ac_line_status == 0)
        return r_power_source::battery;
    return r_power_source::unknown;
}
