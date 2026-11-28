#ifndef __r_vss_r_doorbell_rings_h
#define __r_vss_r_doorbell_rings_h

#include "r_disco/r_camera.h"
#include "r_utils/r_nullable.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace r_vss
{

// Which of a doorbell's presses are rings. A press is its visitor event going active (r_cameras: ONVIF's
// RuleEngine/MyRuleDetector/Visitor, or Reolink's visitor state), or a press its other source reports (an email: see
// r_camera::press_source); an event's end is nothing.
// - From the same source, a press within DEBOUNCE_MS after the doorbell's last ring is that ring: someone pressing
//   twice, or a subscription that restarts mid-press and reports the state again.
// - From another source, a press within ACROSS_SOURCES_MS of the last ring's time, before or after, is that ring: the
//   doorbell told revere twice, by ONVIF and by email (which arrives late, with the press's own time).
// Each source's last press is kept too, to say which sources a doorbell's presses come by. Safe from any thread.
class r_doorbell_rings
{
public:
    enum { DEBOUNCE_MS = 5000, ACROSS_SOURCES_MS = 15000 };

    // True when a press at press_ms, by source, is a ring (and it's remembered as the doorbell's last).
    bool press(const std::string& camera_id, int64_t press_ms, const std::string& source = "onvif")
    {
        std::lock_guard<std::mutex> g(_lock);
        _last_press[camera_id][source] = press_ms;
        auto it = _last.find(camera_id);
        if(it != _last.end())
        {
            auto& last = it->second;
            if(source == last.source && press_ms - last.ms < DEBOUNCE_MS && press_ms >= last.ms)
                return false;
            if(source != last.source && press_ms - last.ms < ACROSS_SOURCES_MS && last.ms - press_ms < ACROSS_SOURCES_MS)
                return false;
        }
        _last[camera_id] = ring{press_ms, source};
        return true;
    }

    // When source last reported a press of this doorbell, ring or not; null if it never has (since revere started).
    r_utils::r_nullable<int64_t> last_press(const std::string& camera_id, const std::string& source) const
    {
        std::lock_guard<std::mutex> g(_lock);
        auto it = _last_press.find(camera_id);
        if(it == _last_press.end())
            return {};
        auto s = it->second.find(source);
        return s == it->second.end() ? r_utils::r_nullable<int64_t>() : r_utils::r_nullable<int64_t>(s->second);
    }

    // The camera is gone, or no longer a doorbell.
    void forget(const std::string& camera_id)
    {
        std::lock_guard<std::mutex> g(_lock);
        _last.erase(camera_id);
        _last_press.erase(camera_id);
    }

private:
    struct ring { int64_t ms; std::string source; };
    mutable std::mutex _lock;
    std::map<std::string, ring> _last;
    std::map<std::string, std::map<std::string, int64_t>> _last_press;
};

// What a camera's event is to a doorbell: nothing (not a doorbell, not a press), a ring, or a press within
// DEBOUNCE_MS of the last ring.
enum class r_doorbell_press { none, ring, repeat };

// camera: the camera as stored (null if it's gone). what and active: the event (r_cameras::r_camera_transition); a
// press from another source is a "visitor" going active too. source: "onvif" (the camera's events), or the other
// source that reported it ("reolink_email").
inline r_doorbell_press doorbell_press(r_doorbell_rings& rings, const r_utils::r_nullable<r_disco::r_camera>& camera,
                                       const std::string& what, bool active, int64_t now_ms,
                                       const std::string& source = "onvif")
{
    if(what != "visitor" || !active || camera.is_null() || r_disco::camera_kind(camera.value()) != "doorbell")
        return r_doorbell_press::none;
    // A source the doorbell isn't set to (its email, once it's set back to ONVIF) is nothing. ONVIF always counts.
    if(source != "onvif" && r_disco::camera_press_source(camera.value()) != source)
        return r_doorbell_press::none;
    return rings.press(camera.value().id, now_ms, source) ? r_doorbell_press::ring : r_doorbell_press::repeat;
}

// A ring as revere stores it in the camera's metadata ("doorbell_rings"): {"ts": <epoch ms>}.
inline std::string doorbell_ring_json(int64_t ts_ms)
{
    return std::string("{\"ts\":") + std::to_string(ts_ms) + "}";
}

}

#endif
