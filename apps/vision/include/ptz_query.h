
#ifndef __vision_ptz_query_h
#define __vision_ptz_query_h

#include <optional>
#include <stdexcept>
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
// for the list_presets probe. A flag is empty when the camera didn't say which
// axes it drives: unknown, so the UI keeps that control, and hides it only on
// an explicit false.
struct ptz_result
{
    std::vector<ptz_preset> presets;
    std::optional<bool> supports_pan_tilt;
    std::optional<bool> supports_zoom;
};

// Each move stops by itself after this unless another arrives, so a lost stop
// (Revere briefly unreachable) or Vision quitting with a direction held can't
// leave the camera moving. A held button re-sends the move every
// PTZ_KEEPALIVE_MS, well inside it, so a long hold keeps moving.
constexpr const char* PTZ_MOVE_TIMEOUT = "PT1S";
constexpr int PTZ_KEEPALIVE_MS = 700;

// Thrown by query_ptz when Revere says the camera has no ONVIF PTZ (HTTP 501):
// unlike a transport failure, that answer doesn't change by asking again.
struct ptz_unsupported : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Runs a PTZ command against Revere's POST /ptz for camera_id:
//   action "move"        -- continuous move at velocity (pan, tilt, zoom), [-1,1]
//   action "stop"        -- stop any motion
//   action "goto_preset" -- recall preset_token
//   action "list_presets"-- returns the camera's presets and axis capabilities
// For every action but list_presets the result's presets/flags are empty.
//
// Throws on a transport error or a non-200 reply: ptz_unsupported when the
// camera has no ONVIF PTZ service (HTTP 501), so a probe can tell "no PTZ"
// from a Revere it couldn't reach. A move carries PTZ_MOVE_TIMEOUT.
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
