#ifndef __r_vss_r_early_alerts_h
#define __r_vss_r_early_alerts_h

#include "r_vss/r_motion_plugin.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace r_vss
{

// A detection's box, in the 640x640 letterbox space detections use.
struct r_box
{
    float x1 {0}, y1 {0}, x2 {0}, y2 {0};
};

R_API float box_iou(const r_box& a, const r_box& b);

// "person", "vehicle" or "animal" for the classes that alert (as revere_cloud
// and belfry group them), "" for the rest.
R_API std::string alert_group_of(const std::string& class_name);

// Which detections the YOLO plugin puts on the detection bus during a motion
// event, rather than at its end. The bus feeds revere_cloud's instant alert,
// so a person at the door used to be pushed only once the whole event was
// over (on 2026-10-20 a person alert took 13 to 48 s on average from the
// detection to the push). Now the first detection of each alerting class in
// an event goes out as soon as YOLO sees it, if:
//  - it is a person, vehicle or animal, and over the motion (the plugin's
//    region filter, unchanged);
//  - its confidence is at least MIN_CONFIDENCE;
//  - it isn't on one of the camera's static anchors: a box where the same
//    class sat still through earlier events (a parked car, bins, leaves
//    YOLO takes for a person). Static scenery like that was most of the
//    false detections on our cameras.
// The end-of-event analysis is unchanged and stays the record: what it
// keeps is still written and emitted then, less what already went early.
// So a detection held back here (below the floor, on an anchor) isn't lost:
// it goes out at the end of the event as before, just not early.
//
// Anchors are learnt at the end of each event from the frames that start and
// end it (the plugin runs YOLO on both): a box of a class in both frames with
// IoU >= STATIC_IOU (today's static-object test) is an anchor at once; one
// with IoU >= SIGHTING_IOU (the box moved a little: leaves, or YOLO's own
// jitter) is a sighting, and SIGHTINGS_FOR_ANCHOR sightings in separate
// events make an anchor. A detection matches an anchor of its class at IoU
// >= ANCHOR_MATCH_IOU. An anchor lapses ANCHOR_TTL_MS after it was last seen,
// and a camera keeps at most MAX_ANCHORS (the oldest go).
//
// Persons are stricter: a visitor standing on the doorstep when two events
// start and end would otherwise anchor the doorstep and hold back every later
// visitor's early alert there, on the camera that needs it most. So a weak
// person anchor takes PERSON_SIGHTINGS_FOR_ANCHOR sightings and lapses
// PERSON_WEAK_TTL_MS after the last: leaves in the wind recur many times an
// hour and anchor; visitors rarely stand in the same place three times in two
// hours, and if they do the anchor is gone two hours later. Strong person
// anchors (still between the start and end frames) keep ANCHOR_TTL_MS: the
// end-of-event analysis already treats such a person as static. to_json and
// from_json keep them across restarts.
//
// Until an event has sent something early, the plugin looks every
// FAST_LOOK_MS rather than SLOW_LOOK_MS, so someone walking into motion
// that's already going is seen within a second.
//
// Thread-safe.
class r_early_alerts
{
public:
    static constexpr float MIN_CONFIDENCE = 0.6f;
    static constexpr float STATIC_IOU = 0.85f;
    static constexpr float SIGHTING_IOU = 0.5f;
    static constexpr float ANCHOR_MATCH_IOU = 0.5f;
    enum
    {
        SIGHTINGS_FOR_ANCHOR = 2,
        PERSON_SIGHTINGS_FOR_ANCHOR = 3,
        MAX_ANCHORS = 32,
        FAST_LOOK_MS = 1000,
        SLOW_LOOK_MS = 5000
    };
    static constexpr int64_t ANCHOR_TTL_MS = 24LL * 60 * 60 * 1000;
    static constexpr int64_t PERSON_WEAK_TTL_MS = 2LL * 60 * 60 * 1000;

    enum r_verdict
    {
        early_sent,        // emit it now
        early_not_alerting, // not a person, vehicle or animal: at the end only
        early_below_floor, // under MIN_CONFIDENCE
        early_anchored,    // on a static anchor
        early_already_sent // this event already sent this class
    };

    struct r_decision
    {
        r_verdict verdict {early_not_alerting};
        float anchor_iou {0}; // the anchor's IoU, for early_anchored
        bool strong_anchor {false};
    };

    struct r_seen
    {
        std::string class_name;
        r_box box;
    };

    R_API void event_started(const std::string& camera_id);
    // Decides for one detection YOLO found over the motion at ts, and
    // records it as sent if it is.
    R_API r_decision consider(const std::string& camera_id, const std::string& class_name, float confidence, const r_box& box, int64_t ts);
    // Whether a detection (class at ts) went out early this event.
    R_API bool sent_early(const std::string& camera_id, const std::string& class_name, int64_t ts) const;
    // How long to wait between looks during this camera's event.
    R_API int64_t look_interval_ms(const std::string& camera_id) const;
    // Learns anchors from the detections (unfiltered) in the frames that
    // started and ended the event; true if the camera's anchors changed.
    R_API bool event_ended(const std::string& camera_id, int64_t ts, const std::vector<r_seen>& start_frame, const std::vector<r_seen>& end_frame);

    R_API size_t anchor_count(const std::string& camera_id, int64_t now) const;
    R_API std::string to_json() const;
    // Replaces the anchors with those in json; ignores what it can't read.
    R_API void from_json(const std::string& json);

private:
    struct anchor
    {
        std::string class_name;
        r_box box;
        int sightings {0};
        bool strong {false};
        int64_t last_seen {0};

        int64_t ttl_ms() const { return (class_name == "person" && !strong) ? PERSON_WEAK_TTL_MS : ANCHOR_TTL_MS; }
        bool lapsed(int64_t now) const { return now - last_seen > ttl_ms(); }
        bool active(int64_t now) const
        {
            return !lapsed(now) && (strong || sightings >= (class_name == "person" ? (int)PERSON_SIGHTINGS_FOR_ANCHOR : (int)SIGHTINGS_FOR_ANCHOR));
        }
    };
    struct event_state
    {
        std::set<std::string> classes_sent;
        std::set<std::pair<std::string, int64_t>> sent;
    };

    mutable std::mutex _mtx;
    std::map<std::string, std::vector<anchor>> _anchors;
    std::map<std::string, event_state> _events;
};

// Whether the motion engine's YOLO probe confirms a camera's first
// significant key frame as an event on its own (see r_motion_engine): a
// person at MIN_CONFIDENCE or more whose box centre is on the motion.
// Persons only: a parked car or bins beside a moving shadow would otherwise
// start an event on one frame. Vehicles and animals still need MOG2's
// confirmation.
struct r_probe_detection
{
    std::string class_name;
    float confidence {0};
    r_box box;
};
R_API bool probe_confirms(const std::vector<r_probe_detection>& detections, const motion_region& motion_bbox, const std::vector<motion_region>& motion_regions);

}

#endif
