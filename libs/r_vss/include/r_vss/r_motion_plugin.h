
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

inline bool valid_motion_region(const motion_region& region)
{
    return region.has_motion && region.width > 0 && region.height > 0;
}

inline bool has_valid_motion_evidence(const motion_region& motion_bbox,
                                      const std::vector<motion_region>& motion_regions)
{
    if(valid_motion_region(motion_bbox))
        return true;

    for(const auto& region : motion_regions)
    {
        if(valid_motion_region(region))
            return true;
    }
    return false;
}

// Prefer the individual connected components when available. Falling back to
// the union box preserves compatibility with motion producers that only know
// how to provide one region; accepting the point when neither exists preserves
// the legacy behavior for endpoint frames with no current motion.
inline bool motion_evidence_contains_point(const motion_region& motion_bbox,
                                           const std::vector<motion_region>& motion_regions,
                                           float x, float y, float margin = 0.0f)
{
    bool has_valid_component = false;
    for(const auto& region : motion_regions)
    {
        if(!valid_motion_region(region))
            continue;

        has_valid_component = true;
        if(x >= static_cast<float>(region.x) - margin &&
           x <= static_cast<float>(region.x + region.width) + margin &&
           y >= static_cast<float>(region.y) - margin &&
           y <= static_cast<float>(region.y + region.height) + margin)
            return true;
    }

    if(has_valid_component)
        return false;

    if(valid_motion_region(motion_bbox))
    {
        return x >= static_cast<float>(motion_bbox.x) - margin &&
               x <= static_cast<float>(motion_bbox.x + motion_bbox.width) + margin &&
               y >= static_cast<float>(motion_bbox.y) - margin &&
               y <= static_cast<float>(motion_bbox.y + motion_bbox.height) + margin;
    }

    return true;
}

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

// stop_plugin: Signals the plugin to stop processing and wait for threads to finish
// Called before destroy_plugin to ensure clean shutdown
R_API void stop_plugin(r_motion_plugin_handle plugin);

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

// Optional v2 entry point. Hosts use it when present and fall back to
// post_motion_event for older plugins. motion_regions_xywh contains four ints
// per region in x, y, width, height order.
R_API void post_motion_event_regions(
    r_motion_plugin_handle plugin,
    int evt,
    const char* camera_id,
    int64_t ts,
    const uint8_t* frame_data,
    size_t frame_data_size,
    uint16_t width,
    uint16_t height,
    int motion_x,
    int motion_y,
    int motion_width,
    int motion_height,
    bool has_motion,
    const int* motion_regions_xywh,
    size_t motion_region_count
);

#ifdef __cplusplus
}
#endif

#endif
