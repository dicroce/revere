
#ifndef __r_vss_r_tier_outputs_h
#define __r_vss_r_tier_outputs_h

#include "r_vss/r_motion_event_plugin_host.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace r_vss
{

// What tiers 2 and 3 write and put on the detection bus, in the shapes the
// YOLO plugin uses, so Revere Cloud's alerts, the stats and the UIs take them
// unchanged.

// The class a camera reported, as written and put on the bus: "person",
// "vehicle" or "animal", the camera's own word (it doesn't say which animal or
// vehicle, so none is guessed); "" for classes no consumer acts on (package,
// visitor, face), which are logged but not reported.
R_API std::string camera_report_class(const std::string& what);

// A camera's report of a class at ts, as a detection: source "camera", no box
// and no confidence (both 0).
R_API r_detection camera_report(const std::string& camera_id, const std::string& class_name, int64_t ts);

// Whether a YOLO class is one consumers act on (a person, a vehicle or an
// animal). The pass reports only these: without motion regions to filter by,
// YOLO's other classes (a "bowl" in a porch) are mostly noise.
R_API bool reported_class(const std::string& class_name);

// The analytics JSON the YOLO plugin writes (boxes normalised from 640x640),
// with "source" naming where it came from ("camera" or "detection_pass"). A
// camera report has no confidence or box (x1..y2) in it.
R_API std::string analytics_json(int64_t start_ms, int64_t end_ms, const std::vector<r_detection>& detections, const std::string& source);

// A camera leaving tier 1 loses MOG2's state with its end event: if a window it opened is still open in `starts`,
// forgets it and says it closed at `now_ms`. Returns whether one was open.
inline bool close_open_mog2_window(std::map<std::string, int64_t>& starts, const std::string& camera_id, int64_t now_ms,
                                   const std::function<void(const std::string&, bool, int64_t)>& notify_motion)
{
    if(starts.erase(camera_id) == 0)
        return false;
    notify_motion(camera_id, false, now_ms);
    return true;
}

}

#endif
