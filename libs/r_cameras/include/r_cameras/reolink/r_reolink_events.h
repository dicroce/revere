
#ifndef __r_cameras_reolink_r_reolink_events_h
#define __r_cameras_reolink_r_reolink_events_h

#include "r_cameras/r_camera_events_base.h"
#include "r_cameras/reolink/r_reolink_api.h"
#include "r_utils/r_macro.h"
#include <map>
#include <string>
#include <vector>

namespace r_cameras
{

// Turns polled Reolink states into starts and ends. The first poll is the
// camera's state as it was found (like ONVIF's Initialized): anything already
// active starts. Reolink's AI types become Revere's classes: people -> person,
// vehicle -> vehicle, dog_cat -> animal; others (face, package...) keep their
// names. The camera is in motion while its motion state or any AI state is
// active (its AI detection runs apart from its motion detection).
class r_reolink_state_tracker
{
public:
    R_API std::vector<r_camera_transition> process(const reolink_state& state);

    // Ends for everything active (polling stopped or failed: the state is unknown).
    R_API std::vector<r_camera_transition> close_all();

    // The classes the camera supports, in Revere's names.
    R_API std::vector<std::string> classes() const;

    R_API static std::string class_name(const std::string& ai_type);

private:
    std::map<std::string, bool> _active;   // "motion" or a class -> active
    std::vector<std::string> _classes;
};

// A Reolink's events from its HTTP API: polls motion and AI state every
// POLL_S, and on failure tries again with backoff (10 s doubling to 10 min).
// Used when the camera's ONVIF events are down (r_camera_events_chain).
class r_reolink_events final : public r_camera_events_base
{
public:
    enum { POLL_S = 1 };

    R_API r_reolink_events(const r_disco::r_camera& camera, transition_fn on_transition);
    R_API ~r_reolink_events() noexcept override;

private:
    void _run() override;

    r_reolink_state_tracker _tracker;
};

}

#endif
