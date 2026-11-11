
#ifndef __r_cameras_onvif_r_onvif_events_h
#define __r_cameras_onvif_r_onvif_events_h

#include "r_cameras/r_camera_events_base.h"
#include "r_cameras/onvif/r_onvif_event_tracker.h"
#include "r_utils/r_macro.h"
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace r_onvif { class r_onvif_cam; }

namespace r_cameras
{

// A camera's events over ONVIF: one pull point subscription (or, for a camera that has no pull point, a push
// subscription: see _push_session). Finds the device
// service (its stored port first), subscribes, pulls every 2 s, renews at half
// the termination time, and resubscribes with backoff (10 s doubling to 10 min)
// on failure. After its first subscription it gives a camera whose motion
// region is empty the whole frame (see fill_empty_cell_motion_regions). A stop
// waits at most about one pull or one connection attempt.
class r_onvif_events final : public r_camera_events_base
{
public:
    R_API r_onvif_events(const r_disco::r_camera& camera, transition_fn on_transition);

    // For tests: how long a subscription is asked to last (120 s), so a renewal (at half of it) can be seen quickly.
    R_API static void set_termination_seconds_for_tests(int seconds);
    // For tests: the least quiet (30 s) after which a camera that has been sending a heartbeat is taken to have lost its push subscription.
    R_API static void set_heartbeat_silence_floor_for_tests(int seconds);
    R_API ~r_onvif_events() noexcept override;

private:
    void _run() override;

    // The camera's motion region is checked (once), and an empty one given the whole frame.
    void _check_region(r_onvif::r_onvif_cam& cam, const std::string& capabilities);

    // For a camera that doesn't offer a pull point: Subscribe with Revere's own address as the consumer, the camera
    // POSTs each Notify to Revere's listener (r_onvif_push_listener), renewed at half the termination time, and
    // ended with Unsubscribe. Returns when asked to stop, and throws when the subscription fails, as a pull does.
    void _push_session(r_onvif::r_onvif_cam& cam, const std::string& events_url, const std::string& capabilities, int port,
                       std::string& stage, int& backoff_s);

    r_onvif_event_tracker _tracker;
    bool _region_checked {false};   // the camera's motion region has been checked (once)
    bool _push_preferred {false};   // the camera said it has no pull point: don't ask again

    // Notify bodies the listener has received, not yet parsed (parsed on this thread, not the listener's).
    std::mutex _notify_lock;
    std::condition_variable _notify_cv;
    std::vector<std::string> _notifies;
};

}

#endif
