
#ifndef __r_vss_r_tier_outputs_h
#define __r_vss_r_tier_outputs_h

#include "r_vss/r_motion_event_plugin_host.h"
#include "r_vss/r_early_alerts.h"
#include "r_vss/r_detection_planner.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
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

// MOG2's end event: if a window is open in `starts`, forgets it, says it closed at `ts`, and returns when it opened.
// Nothing when none is open, as when close_open_mog2_window already closed it (a removal, whose end event the motion
// engine still sends), so the plugins are told it stopped once.
inline std::optional<int64_t> end_mog2_window(std::map<std::string, int64_t>& starts, const std::string& camera_id, int64_t ts,
                                              const std::function<void(const std::string&, bool, int64_t)>& notify_motion)
{
    auto it = starts.find(camera_id);
    if(it == starts.end())
        return std::nullopt;
    auto opened = it->second;
    starts.erase(it);
    notify_motion(camera_id, false, ts);
    return opened;
}

// Tier 3's parked cars. The detection pass keeps its own static anchors (an
// r_early_alerts, as the YOLO plugin does on tier 1), so a car parked in view
// stops alerting and colouring other events once it has been seen to sit
// still. Without them, a far car's jittering box was news in every session.

// Which of a session's first finds go on the bus at once: one on an active
// anchor of its class is held, as tier 1's early alert holds it, and goes at
// the session's end if the record keeps it. notes, if given: a line for each
// one held, and for each vehicle sent (with the nearest anchor).
R_API std::vector<r_detection> pass_alert_now(const r_early_alerts& anchors, const std::string& camera_id, const std::vector<r_detection>& found,
                                              std::vector<std::string>* notes = nullptr);

// The session's detections the record keeps: less each vehicle on an active
// anchor that was already there before the session (in the pre-roll or the
// first frame), as is_parked_vehicle decides. notes, if given: for each
// vehicle class, how many were kept and left out, and for the first kept,
// its box and the nearest anchor.
R_API std::vector<r_detection> pass_record(const r_early_alerts& anchors, const r_detection_session& session,
                                           std::vector<std::string>* notes = nullptr);

// Learns the anchors from the session's first and last frames, as tier 1
// learns from an event's (r_early_alerts::event_ended); true if they changed.
R_API bool pass_learn(r_early_alerts& anchors, const r_detection_session& session, std::vector<std::string>* notes = nullptr);

}

#endif
