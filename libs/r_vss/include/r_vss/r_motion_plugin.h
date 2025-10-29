
#ifndef __r_motion_plugin_h
#define __r_motion_plugin_h

#include "r_vss/r_motion_event.h"
#include "r_utils/r_macro.h"
#include <string>
#include <cstdint>
#include <vector>

namespace r_vss
{

struct motion_region {
    int x, y, width, height;
    bool has_motion;
};

class r_motion_plugin
{
public:
    R_API virtual void post_motion_event(r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height, const motion_region& motion_bbox) = 0;
};

}

// Pure C API for plugin loading - no C++ types in extern "C" interface
// This allows plugins to be loaded across compiler boundaries safely

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle types - actual types are only known to C++ code
typedef void* r_motion_plugin_handle;
typedef void* r_motion_event_plugin_host_handle;

// Plugin entry points that must be implemented by each plugin
// load_plugin: Creates and returns a new plugin instance
R_API r_motion_plugin_handle load_plugin(r_motion_event_plugin_host_handle host);

// destroy_plugin: Destroys a plugin instance created by load_plugin
R_API void destroy_plugin(r_motion_plugin_handle plugin);

// post_motion_event: Called by host when motion is detected
// This is the callback that plugins must implement to receive motion events
R_API void post_motion_event(
    r_motion_plugin_handle plugin,
    int evt,                      // r_motion_event as int (0=start, 1=update, 2=end)
    const char* camera_id,
    int64_t ts,
    const uint8_t* frame_data,    // BGR format: 3 bytes per pixel
    size_t frame_data_size,
    uint16_t width,
    uint16_t height,
    int motion_x,                 // motion_region fields
    int motion_y,
    int motion_width,
    int motion_height,
    bool has_motion
);

#ifdef __cplusplus
}
#endif

#endif
