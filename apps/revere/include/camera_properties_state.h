#ifndef __revere_camera_properties_state_h
#define __revere_camera_properties_state_h

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace revere
{

// What a camera's properties looked like when the pane opened, so the pane knows whether anything changed (Save is
// off until it has) and whether leaving would lose something.
struct camera_properties_session
{
    std::string camera_id;
    bool doorbell {false};
    bool motion {false};
    bool prune {false};
    std::string hours;
    std::string tier;
    std::string transport;
    // The hours field's text.
    char hours_text[16] {0};
    // Save was tried: show what's wrong with the hours.
    bool tried {false};

    void begin(const std::string& id, bool doorbell_on, bool motion_on, bool prune_on, const std::string& hours_now, const std::string& tier_now,
               const std::string& transport_now)
    {
        camera_id = id;
        doorbell = doorbell_on;
        motion = motion_on;
        prune = prune_on;
        hours = hours_now;
        tier = tier_now;
        transport = transport_now;
        std::snprintf(hours_text, sizeof(hours_text), "%s", hours_now.c_str());
        tried = false;
    }

    // The hours field holds a whole number of hours, 1 or more.
    bool hours_ok() const
    {
        const std::string h(hours_text);
        return !h.empty() && h.size() <= 6 && std::all_of(h.begin(), h.end(), [](unsigned char c) { return std::isdigit(c); }) && std::stol(h) >= 1;
    }

    // Anything differs from when the pane opened. The hours only count while pruning is on (they aren't saved otherwise).
    bool dirty(bool doorbell_now, bool motion_now, bool prune_now, const std::string& tier_now, const std::string& transport_now, bool streams_changed) const
    {
        if(streams_changed || doorbell_now != doorbell || motion_now != motion || prune_now != prune || tier_now != tier || transport_now != transport)
            return true;
        return prune_now && std::string(hours_text) != hours;
    }
};

}

#endif
