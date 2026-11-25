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
// RuleEngine/MyRuleDetector/Visitor, or Reolink's visitor state); its end is nothing. A press within DEBOUNCE_MS of
// the doorbell's last ring is the same ring: someone pressing twice, or a subscription that restarts mid-press and
// reports the state again. Safe from any thread.
class r_doorbell_rings
{
public:
    enum { DEBOUNCE_MS = 5000 };

    // True when a press at now_ms is a ring (and it's remembered as the doorbell's last).
    bool press(const std::string& camera_id, int64_t now_ms)
    {
        std::lock_guard<std::mutex> g(_lock);
        auto it = _last.find(camera_id);
        if(it != _last.end() && now_ms - it->second < DEBOUNCE_MS && now_ms >= it->second)
            return false;
        _last[camera_id] = now_ms;
        return true;
    }

    // The camera is gone, or no longer a doorbell.
    void forget(const std::string& camera_id)
    {
        std::lock_guard<std::mutex> g(_lock);
        _last.erase(camera_id);
    }

private:
    std::mutex _lock;
    std::map<std::string, int64_t> _last;
};

// What a camera's event is to a doorbell: nothing (not a doorbell, not a press), a ring, or a press within
// DEBOUNCE_MS of the last ring.
enum class r_doorbell_press { none, ring, repeat };

// camera: the camera as stored (null if it's gone). what and active: the event (r_cameras::r_camera_transition).
inline r_doorbell_press doorbell_press(r_doorbell_rings& rings, const r_utils::r_nullable<r_disco::r_camera>& camera,
                                       const std::string& what, bool active, int64_t now_ms)
{
    if(what != "visitor" || !active || camera.is_null() || r_disco::camera_kind(camera.value()) != "doorbell")
        return r_doorbell_press::none;
    return rings.press(camera.value().id, now_ms) ? r_doorbell_press::ring : r_doorbell_press::repeat;
}

// A ring as revere stores it in the camera's metadata ("doorbell_rings"): {"ts": <epoch ms>}.
inline std::string doorbell_ring_json(int64_t ts_ms)
{
    return std::string("{\"ts\":") + std::to_string(ts_ms) + "}";
}

}

#endif
