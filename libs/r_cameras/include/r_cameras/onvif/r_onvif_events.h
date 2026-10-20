
#ifndef __r_cameras_onvif_r_onvif_events_h
#define __r_cameras_onvif_r_onvif_events_h

#include "r_cameras/r_camera_events_base.h"
#include "r_cameras/onvif/r_onvif_event_tracker.h"
#include "r_utils/r_macro.h"

namespace r_cameras
{

// A camera's events over ONVIF: one pull point subscription. Finds the device
// service (its stored port first), subscribes, pulls every 2 s, renews at half
// the termination time, and resubscribes with backoff (10 s doubling to 10 min)
// on failure. After its first subscription it gives a camera whose motion
// region is empty the whole frame (see fill_empty_cell_motion_regions). A stop
// waits at most about one pull or one connection attempt.
class r_onvif_events final : public r_camera_events_base
{
public:
    R_API r_onvif_events(const r_disco::r_camera& camera, transition_fn on_transition);
    R_API ~r_onvif_events() noexcept override;

private:
    void _run() override;

    r_onvif_event_tracker _tracker;
    bool _region_checked {false};   // the camera's motion region has been checked (once)
};

}

#endif
