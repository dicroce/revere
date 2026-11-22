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
// A box for the log: "[x1,y1 x2,y2] WxH", whole pixels of the letterbox.
R_API std::string box_text(const r_box& b);

// Whether two boxes of a class are the same thing in the same place: IoU at
// least iou, or, for a vehicle, YOLO's jitter on a small box. A far car only
// 20x12 px in the letterbox drops under IoU 0.5 when its box moves 8 px, so
// IoU alone never saw it sit still. A vehicle's boxes also match when their
// centres are within VEHICLE_JITTER_PX (and half the box) on each axis and
// each side is within VEHICLE_JITTER_SIZE of the other's. On a big box that
// much jitter keeps a high IoU anyway; a car driving past moves further.
// Persons and animals: IoU only (a far person walking slowly moves little).
constexpr float VEHICLE_JITTER_PX = 8.0f;
constexpr float VEHICLE_JITTER_SIZE = 1.5f;
R_API bool same_place(const std::string& class_name, const r_box& a, const r_box& b, float iou);

// Whether part lies mostly inside whole: CONTAINED_FRACTION of part's area. How
// a parked pickup's cab, all YOLO boxes while a
// person stands at its door, is told to be part of the pickup (its IoU with the
// whole truck's box is 0.12 to 0.16).
constexpr float CONTAINED_FRACTION = 0.8f;
R_API bool mostly_inside(const r_box& part, const r_box& whole);

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
        bool part_of_vehicle {false}; // early_anchored as part of a parked vehicle (part_of_parked_vehicle)
    };

    struct r_seen
    {
        std::string class_name;
        r_box box;
    };

    // An event started. `continues`: it carries straight on from the camera's previous one (motion_event_continue:
    // the same motion, cut at the max event length), so the classes that chain already sent early stay sent. A
    // person in motion that goes on for an hour is pushed once, not every five minutes; a vehicle that turns up later
    // in it is still new.
    R_API void event_started(const std::string& camera_id, bool continues = false);
    // Decides for one detection YOLO found over the motion at ts, and
    // records it as sent if it is.
    R_API r_decision consider(const std::string& camera_id, const std::string& class_name, float confidence, const r_box& box, int64_t ts);
    // Whether a detection is on one of the camera's active anchors of its
    // class, as consider() holds it (early_anchored or early_not_alerting),
    // with no floor and nothing recorded: for the detection pass on tier 3,
    // which sends once per session itself.
    // With same_frame (the frame's other detections), a vehicle box that is only part of a parked vehicle
    // (part_of_parked_vehicle) is anchored too.
    R_API r_decision anchored(const std::string& camera_id, const std::string& class_name, const r_box& box, int64_t ts,
                              const std::vector<r_seen>* same_frame = nullptr) const;
    // Whether a vehicle box is only part of the vehicle on one of the camera's strong vehicle anchors: mostly inside
    // the anchor (mostly_inside), while nothing else in the same frame (same_frame) is at the anchor itself, so the
    // whole vehicle isn't seen there, only that part (a person at the cab's door hides the rest). Any vehicle class
    // either side: YOLO may call a pickup's cab a car. A car pulling up in front of a parked truck is seen with the
    // truck, so it isn't part of it.
    R_API bool part_of_parked_vehicle(const std::string& camera_id, const std::string& class_name, const r_box& box, int64_t ts,
                                      const std::vector<r_seen>& same_frame) const;
    // Whether a detection (class at ts) went out early this event.
    R_API bool sent_early(const std::string& camera_id, const std::string& class_name, int64_t ts) const;
    // Whether a vehicle at box (at ts) sits on one of the camera's learnt anchors: a parked car seen in an event
    // that was about something else. The event's record leaves such a detection out, so it doesn't colour a
    // raccoon's event as a vehicle's. Vehicles only: animals and persons come and go, and an animal anchor is a
    // feeding station or a favourite chair, where the record should keep every sighting.
    // An anchor stays active for a day and is not cleared when the car leaves, so the car must also have been on
    // the anchor in the event's start frame (start_frame): parked before the event began. A car arriving into
    // the spot during the event, or a visitor taking it, is kept.
    // With same_frame, a box that is only part of a parked vehicle (part_of_parked_vehicle) is parked too, if the
    // whole vehicle was there in start_frame.
    R_API bool is_parked_vehicle(const std::string& camera_id, const std::string& class_name, const r_box& box, int64_t ts, const std::vector<r_seen>& start_frame,
                                 const std::vector<r_seen>* same_frame = nullptr) const;
    // How long to wait between looks during this camera's event.
    R_API int64_t look_interval_ms(const std::string& camera_id) const;
    // Learns anchors from the detections (unfiltered) in the frames that
    // started and ended the event; true if the camera's anchors changed.
    // notes, if given, gets a line for each person, vehicle or animal in the
    // start frame: its best match in the end frame and what that did to the
    // anchors (nothing, a new one, or one seen again), for the log.
    R_API bool event_ended(const std::string& camera_id, int64_t ts, const std::vector<r_seen>& start_frame, const std::vector<r_seen>& end_frame,
                           std::vector<std::string>* notes = nullptr);
    // For the log, when a detection wasn't held or left out: the camera's
    // anchor of its class that best overlaps box, active or not, as
    // "nearest car anchor [..] WxH: IoU 0.31, 1 sighting, weak, not active",
    // or "no car anchor".
    R_API std::string nearest_anchor(const std::string& camera_id, const std::string& class_name, const r_box& box, int64_t ts) const;

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

    // consider()'s anchor test. PRECONDITION: _mtx held.
    r_decision _anchored_locked(const std::string& camera_id, const std::string& class_name, const r_box& box, int64_t ts) const;
    // The strong vehicle anchor box is part of (part_of_parked_vehicle), or null. PRECONDITION: _mtx held.
    const anchor* _containing_anchor_locked(const std::string& camera_id, const std::string& class_name, const r_box& box, int64_t ts,
                                            const std::vector<r_seen>& same_frame) const;

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
