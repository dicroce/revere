
#ifndef __r_vss_r_detection_planner_h
#define __r_vss_r_detection_planner_h

#include "r_vss/r_motion_event_plugin_host.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace r_vss
{

// Which frame a look wants.
enum r_look_kind
{
    look_first,      // the first look at a session: the newest live key frame, ~2 s after motion started
    look_follow_up,  // a later live look while the motion lasts
    look_pre_roll,   // a stored key frame from just before the session: what was already there
    look_short,      // a stored key frame from a short event (it ended before the first look): the last one at or before its end
    look_end         // a stored key frame from the end of an event
};

R_API const char* look_kind_name(r_look_kind kind);

struct r_look
{
    std::string camera_id;
    r_look_kind kind {look_first};
    int64_t ts {0};      // stored looks: the time wanted (the key frame at or before it); live looks: when due
    int64_t due_ms {0};
    int attempt {0};
};

// One session's outcome: a run of camera motion (events closer together than
// the cluster gap are one session) and what YOLO found in it.
struct r_detection_session
{
    std::string camera_id;
    int64_t start_ms {0};
    int64_t end_ms {0};
    std::vector<std::string> camera_classes;  // what the camera itself reported during the session
    std::vector<r_detection> detections;      // kept: neither already there before nor static
    int looks {0};                            // looks that produced a frame
    int failed_looks {0};
    int dropped_static {0};                   // detections dropped as things that were already there
    int64_t first_look_ms {0};                // when the first look's result came, 0 if none
    std::vector<r_detection> first_look;      // its detections, unfiltered (what an alert would see)
};

// Plans the detection pass over camera-reported motion (milestone 4): when to
// look at which frame, and what to keep. No threads, no clock and no frames:
// the caller says what happened and when, asks for the next look that is due,
// and reports each look's detections back.
//
//  - Motion start opens a session, or reopens the camera's last one if it
//    ended less than CLUSTER_GAP_MS ago (a person pacing, leaves in wind).
//  - The first look is due FIRST_LOOK_MS after the start, on the newest live
//    key frame, then follow-ups while the motion lasts: every FOLLOW_UP_MS,
//    every FOLLOW_UP_FOUND_MS once something has been found.
//  - A pre-roll look at a stored key frame from PRE_ROLL_MS before the start
//    shows what was there before anything moved.
//  - When an event ends: one look at its end frame, or, if it ended before the
//    first look, at its last key frame (the key frame at or before its end:
//    only key frames are decoded, and an earlier one could predate the event).
//    Both come from storage, STORAGE_DELAY_MS after the time wanted, so the
//    frame is written.
//  - A session finishes CLUSTER_GAP_MS after its last motion ends, once its
//    looks are done. Detections that match one in the pre-roll, or that sit in
//    the same place in the session's first and last frames (IoU >= 0.85, the
//    same class), are dropped as things that were already there: camera
//    events have no motion regions to filter by.
//  - Looks are chosen first looks first, then follow-ups, then stored looks;
//    within a kind, the camera served longest ago goes first, so one busy
//    camera can't starve the rest.
class r_detection_planner
{
public:
    enum
    {
        FIRST_LOOK_MS = 2000,
        FOLLOW_UP_MS = 4000,
        FOLLOW_UP_FOUND_MS = 12000,
        CLUSTER_GAP_MS = 30000,
        PRE_ROLL_MS = 3000,
        STORAGE_DELAY_MS = 2000,
        RETRY_MS = 3000,
        MAX_ATTEMPTS = 2,
        MAX_LOOKS = 30,      // per session; a camera in motion for minutes stops costing a decode every few seconds
        EARLY_CLASS_MS = 5000
    };

    // Camera motion from a source ("" when the camera has one video source).
    // The camera is in motion while any of its sources is.
    R_API void motion(const std::string& camera_id, const std::string& source, bool active, int64_t now_ms);

    // A class the camera reported (person, vehicle...), noted on its session.
    // Cameras often report the class just before the motion, so one that
    // comes up to EARLY_CLASS_MS before a session starts counts for it.
    R_API void camera_class(const std::string& camera_id, const std::string& what, int64_t now_ms);

    // The look to do now, if any; it is taken off the plan until look_done().
    R_API r_utils::r_nullable<r_look> next_look(int64_t now_ms);

    // When the next look falls due (0 = nothing planned).
    R_API int64_t next_due_ms() const;

    // A look's outcome. frame_ts is the time of the frame actually used.
    R_API void look_done(const r_look& look, bool got_frame, int64_t frame_ts, const std::vector<r_detection>& detections, int64_t now_ms);

    // Sessions that finished; each is returned once.
    R_API std::vector<r_detection_session> finished(int64_t now_ms);

    // Same class and IoU >= 0.85 (boxes in 640x640 space).
    R_API static bool same_object(const r_detection& a, const r_detection& b);

private:
    struct _session
    {
        r_detection_session result;
        std::set<std::string> active_sources;
        int64_t event_start_ms {0};     // the current (or last) event's start
        int64_t last_end_ms {0};
        bool live_ok {false};           // a live look has produced a frame
        bool found {false};             // something has been detected
        int planned {0};                // looks planned so far, against MAX_LOOKS
        int64_t next_live_ms {0};       // 0 = no live look planned
        bool live_in_flight {false};
        std::vector<r_look> stored;     // planned stored looks
        int in_flight {0};              // looks taken and not yet done
        std::vector<r_detection> pre_roll;
        std::vector<r_detection> seen;  // every detection from the looks at the session itself
        std::vector<r_detection> first_frame, last_frame;
        int64_t first_frame_ts {-1}, last_frame_ts {-1};
    };

    bool _active(const _session& s) const { return !s.active_sources.empty(); }
    bool _done(const _session& s, int64_t now_ms) const;
    void _plan_live(_session& s, int64_t due_ms);
    void _plan_stored(_session& s, r_look_kind kind, int64_t ts, int64_t due_ms);
    r_detection_session _finish(_session& s);

    std::map<std::string, _session> _sessions;            // camera id -> its open session
    std::vector<r_detection_session> _finished;           // finished, not yet returned
    std::map<std::string, int64_t> _last_served;          // camera id -> when it last got a look
    std::map<std::string, std::deque<std::pair<std::string, int64_t>>> _early_classes;  // camera id -> (class, when), before a session
};

}

#endif
