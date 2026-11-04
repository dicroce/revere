
#ifndef __r_vss_r_tier_outputs_h
#define __r_vss_r_tier_outputs_h

#include "r_vss/r_motion_event_plugin_host.h"
#include "r_vss/r_detection_planner.h"
#include "r_utils/r_macro.h"
#include <cstdint>
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

// A tier-2 camera's report of a class, from its start to its end, held until
// the detection pass has looked at it.
struct r_camera_report_span
{
    std::string camera_id;
    std::string class_name;   // camera_report_class: "person", "vehicle" or "animal"
    int64_t start_ms {0};
    int64_t end_ms {0};
};

// A held report waits at most this long after it ends for the pass's session
// over it (which finishes CLUSTER_GAP_MS after the report ends, once its looks
// are done) before it's written as it stands.
constexpr int64_t CAMERA_REPORT_WAIT_MS = 10 * 60 * 1000;

// A finished detection pass session's verdict on the held reports: those it
// covers (the camera's, starting from EARLY_CLASS_MS before the session to its
// end) are taken out of pending, and the ones its detections don't confirm (no
// kept detection of the report's group: an "animal" report and a "cat") are
// returned, to be written as the camera's own reports, with no box. A
// confirmed report is in the pass's detections, with their boxes and classes.
R_API std::vector<r_camera_report_span> unconfirmed_reports(std::vector<r_camera_report_span>& pending, const r_detection_session& session);

// Held reports that ended CAMERA_REPORT_WAIT_MS or more before now_ms with no
// session over them (the pass stopped, the camera changed tier): taken out of
// pending and returned, to be written as they stand.
R_API std::vector<r_camera_report_span> stale_reports(std::vector<r_camera_report_span>& pending, int64_t now_ms);

// The analytics JSON the YOLO plugin writes (boxes normalised from 640x640),
// with "source" naming where it came from ("camera" or "detection_pass"). A
// camera report has no confidence or box (x1..y2) in it.
R_API std::string analytics_json(int64_t start_ms, int64_t end_ms, const std::vector<r_detection>& detections, const std::string& source);

}

#endif
