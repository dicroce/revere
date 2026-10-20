
#ifndef __r_cameras_onvif_r_onvif_event_tracker_h
#define __r_cameras_onvif_r_onvif_event_tracker_h

#include "r_cameras/r_camera_events.h"
#include "r_onvif/r_onvif_session.h"
#include "r_utils/r_macro.h"
#include <map>
#include <string>
#include <vector>

namespace r_cameras
{

// What a camera's ONVIF events mean to Revere, without any networking: maps
// topics to motion and to detection classes, keeps each one's state, and turns
// changes into starts and ends.
//
// Every topic is a state that flips (motion true, then false). The first pull
// of a subscription reports every topic's current state as "Initialized"; real
// changes arrive as "Changed". Both are compared with the state already known,
// so a subscription that drops mid-event is corrected by the next one's
// Initialized state instead of leaving the event open.
//
// Cameras raise one motion on several topics at once (the Axis fires five for
// every burst), and which topics actually fire depends on the camera and its
// settings: Office announces CellMotionDetector but, with a region of interest
// set, reports motion only on VideoSource/MotionAlarm. So motion is the camera's
// as a whole: in motion while any of its motion topics (RuleEngine/
// MotionRegionDetector, RuleEngine/CellMotionDetector, VideoSource/MotionAlarm),
// from any source, is active; one start when the first goes active, one end
// when the last goes inactive. Motion transitions carry an empty source.

// Transitions carry the camera's topic as received, and for classes the
// event's source items ("Name=Value ...": several video sources stay apart).
class r_onvif_event_tracker
{
public:
    // A topic (normalized, matched as a prefix), the data item carrying its state, and what it means.
    struct rule;

    // Transitions caused by one pull's events, in order.
    R_API std::vector<r_camera_transition> process(const std::vector<r_onvif::onvif_event>& events);

    // Ends for everything still active (the subscriber is stopping, or an event
    // has run longer than anything real).
    R_API std::vector<r_camera_transition> close_all();

    // The motion topic that most recently went active (normalized, e.g.
    // "VideoSource/MotionAlarm"); empty until one has.
    R_API std::string motion_topic() const;

    // The detection classes the camera has reported on, in any state.
    R_API std::vector<std::string> classes() const;

    // A topic with every "prefix:" removed from its path segments
    // ("tns1:RuleEngine/tt:CellMotionDetector" -> "RuleEngine/CellMotionDetector").
    R_API static std::string normalize_topic(const std::string& topic);

private:
    const rule* _match(const std::string& normalized_topic, const r_onvif::onvif_event& ev, std::string& value) const;

    std::map<std::string, bool> _motion_state;  // motion topic + "|" + source -> active
    bool _motion_active {false};                // any of them
    std::string _motion_topic;
    std::string _motion_raw_topic;              // as received, for the transitions
    std::map<std::string, bool> _state;  // class + "|" + source -> active
    std::map<std::string, std::string> _topic_of;  // same key -> topic, for close_all
    std::vector<std::string> _classes;
};

}

#endif
