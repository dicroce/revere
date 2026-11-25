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
    // "main" or "sub": the stream the camera records from (r_camera::record_from).
    std::string record_from;
    // The hours field's text.
    char hours_text[16] {0};
    // Save was tried: show what's wrong with the hours.
    bool tried {false};

    void begin(const std::string& id, bool doorbell_on, bool motion_on, bool prune_on, const std::string& hours_now, const std::string& tier_now,
               const std::string& transport_now, const std::string& record_from_now)
    {
        camera_id = id;
        doorbell = doorbell_on;
        motion = motion_on;
        prune = prune_on;
        hours = hours_now;
        tier = tier_now;
        transport = transport_now;
        record_from = record_from_now;
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
    bool dirty(bool doorbell_now, bool motion_now, bool prune_now, const std::string& tier_now, const std::string& transport_now,
               const std::string& record_from_now, bool streams_changed) const
    {
        if(streams_changed || doorbell_now != doorbell || motion_now != motion || prune_now != prune || tier_now != tier || transport_now != transport ||
           record_from_now != record_from)
            return true;
        return prune_now && std::string(hours_text) != hours;
    }
};


// What "Record from" says under its choice (the stream the camera records from, r_camera::record_from): what it does.
inline std::string record_from_description(const std::string& record_from)
{
    return record_from == "sub"
        ? "Records, shows live, finds motion and takes pictures from the camera's smaller stream, and never opens its main stream, so a camera on a weak link "
          "(Wi-Fi, say) can keep up. The picture is smaller, and there is no HD."
        : "Records from the camera's main stream. Choose the smaller stream for a camera on a weak link that keeps falling behind.";
}

// A line under it when what is set is not what happens: set to the smaller stream, which can't be used now (it is off,
// not found, or smaller streams are off for this computer), so the camera records from its main stream and goes back
// when the smaller one works. `available`: the camera's smaller stream works as saved.
inline std::string record_from_status(const std::string& record_from, bool available)
{
    if(record_from == "sub" && !available)
        return "The camera's smaller stream isn't available now, so it records from the main stream; it goes back to the smaller one when that works.";
    if(record_from != "sub" && !available)
        return "The smaller stream has to be set up and working first (Streams and connection, below; smaller streams also have to be on in Settings), and saved.";
    return std::string();
}

}

#endif
