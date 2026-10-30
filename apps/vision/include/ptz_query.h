
#ifndef __vision_ptz_query_h
#define __vision_ptz_query_h

#include <string>
#include <vector>

// Declared on its own (no ImGui, no imgui_ui.h) so both the UI module
// (ptz_controls.h) and the transport (query.cpp) can include it without the
// include cycle that query.h <-> imgui_ui.h would create.

namespace vision
{

struct ptz_preset
{
    std::string token;
    std::string name;
};

// The reply to a /ptz call. presets and the capability flags are filled only
// for the list_presets probe; the flags say which axes the camera can drive,
// so the UI shows zoom controls only when supports_zoom is true.
struct ptz_result
{
    std::vector<ptz_preset> presets;
    bool supports_pan_tilt {false};
    bool supports_zoom {false};
};

// Runs a PTZ command against Revere's POST /ptz for camera_id:
//   action "move"        -- continuous move at velocity (pan, tilt, zoom), [-1,1]
//   action "stop"        -- stop any motion
//   action "goto_preset" -- recall preset_token
//   action "list_presets"-- returns the camera's presets and axis capabilities
// For every action but list_presets the result's presets/flags are empty.
//
// Throws on a transport error or a non-200 reply. A camera that has no ONVIF
// PTZ service answers list_presets with an error, so a thrown exception from
// a list_presets probe also means "this camera has no PTZ".
ptz_result query_ptz(
    const std::string& ip_address,
    const std::string& camera_id,
    const std::string& action,
    float pan,
    float tilt,
    float zoom,
    const std::string& preset_token
);

}

#endif
