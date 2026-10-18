
#ifndef __r_vss_r_tier_controller_h
#define __r_vss_r_tier_controller_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace r_vss
{

// Where a camera's motion and object classes come from:
//  1: Revere's own: MOG2 motion, YOLO on it (today).
//  2: the camera's: its motion events and its own person/vehicle/animal events.
//  3: the camera's motion events, with Revere's YOLO detection pass on them.
enum r_motion_tier
{
    motion_tier_1 = 1,
    motion_tier_2 = 2,
    motion_tier_3 = 3
};

struct r_tier_status
{
    int tier {motion_tier_1};
    bool automatic {true};      // false: forced by the user
    std::string reason;         // why it is on this tier, for the UI and logs
    int checked {0};            // MOG2 events compared with the camera's events
    int matched {0};            // of those, ones the camera also reported
    int person_checked {0};     // of those, ones where YOLO found a person
    int person_matched {0};     // of those, ones where the camera reported a person too
};

// Chooses each camera's tier (milestone 5), by verifying the camera's events
// rather than trusting what it advertises (Driveway advertised motion and
// people while a no-alarm area kept it silent). No threads and no clock: the
// caller reports what happened and when, and calls tick() regularly.
//
// Automatic (the default):
//  - Every camera starts on tier 1. Each finished MOG2 event is checked
//    VERIFY_DELAY_MS after it ends (YOLO's result comes after the event): did
//    the camera report motion within MATCH_SLACK_MS of it, and, when YOLO
//    found a person, did the camera report a person?
//  - Once at least MIN_EVENTS of the last WINDOW events were checked, at least
//    MATCH_RATE of them matched, and the subscription has been up for
//    STABLE_MS, the camera moves up: to tier 2 if at least MIN_PERSON_EVENTS
//    had a person and the camera reported MATCH_RATE of them, else tier 3.
//  - It drops back to tier 1 when its subscription has been down for
//    DOWN_MS (MOG2 takes over: motion never stops), or when the camera has
//    reported no motion for SILENCE_MS (it may have gone quiet the way
//    Driveway did; tier 1 checks it again from scratch).
// A forced tier 2 or 3 also falls back to tier 1 while the subscription is
// down; forced tier 1 stays.
class r_tier_controller
{
public:
    enum
    {
        WINDOW = 20,
        MIN_EVENTS = 8,
        MIN_PERSON_EVENTS = 3,
        MATCH_SLACK_MS = 3000,
        VERIFY_DELAY_MS = 30000,
        STABLE_MS = 5 * 60 * 1000,
        DOWN_MS = 60 * 1000,
        SILENCE_MS = 12 * 60 * 60 * 1000
    };
    static constexpr double MATCH_RATE = 0.8;

    // "auto" (or empty) or "1", "2", "3"; anything else counts as auto.
    R_API void set_setting(const std::string& camera_id, const std::string& setting);

    // The camera has a working event subscription (state "subscribed") or not.
    // A camera never reported on (no subscription at all) stays on tier 1.
    R_API void subscription(const std::string& camera_id, bool up, int64_t now_ms);

    R_API void camera_motion(const std::string& camera_id, bool active, int64_t now_ms);
    R_API void camera_class(const std::string& camera_id, const std::string& what, bool active, int64_t now_ms);

    // A finished MOG2 event (only tier 1 cameras run MOG2).
    R_API void mog2_event(const std::string& camera_id, int64_t start_ms, int64_t end_ms);

    // A detection YOLO put on the bus (tier 1: from the motion plugin).
    R_API void yolo_detection(const std::string& camera_id, const std::string& class_name, int64_t ts);

    // Checks due MOG2 events and moves cameras between tiers. Returns the
    // cameras whose tier changed.
    R_API std::vector<std::string> tick(int64_t now_ms);

    R_API int tier(const std::string& camera_id) const;
    R_API r_tier_status status(const std::string& camera_id) const;

    R_API void remove(const std::string& camera_id);

private:
    struct _span { int64_t start {0}; int64_t end {0}; };  // end 0 = still going
    struct _check { bool motion {false}; bool person {false}; bool camera_person {false}; };
    struct _camera
    {
        int forced {0};                       // 0 = automatic
        bool known {false};                   // a subscription has been reported
        bool up {false};
        int64_t up_since {0}, down_since {0};
        int64_t last_motion_ms {0};           // the camera's last motion start or end
        int tier {motion_tier_1};
        int verified_tier {0};                // automatic: 2 or 3 once verified, 0 until then
        int64_t verified_at {0};
        std::string reason {"checking the camera's events against Revere's"};
        std::deque<_span> motion, person;     // the camera's recent spans
        std::deque<std::pair<int64_t, int64_t>> pending;  // MOG2 events waiting to be checked
        std::deque<int64_t> yolo_person;      // when YOLO found a person
        std::deque<_check> window;
    };

    static r_tier_status status_of(const _camera& c);
    static bool _overlaps(const std::deque<_span>& spans, int64_t start, int64_t end, int64_t now_ms);
    void _check_pending(_camera& c, int64_t now_ms);
    void _verify(_camera& c);
    int _decide(_camera& c, int64_t now_ms);
    void _prune(_camera& c, int64_t now_ms);

    std::map<std::string, _camera> _cameras;
};

}

#endif
