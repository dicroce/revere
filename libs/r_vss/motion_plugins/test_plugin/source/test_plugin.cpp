#include "test_plugin.h"
#include "r_utils/r_logger.h"
#include "r_vss/r_motion_event_plugin_host.h"
#include <memory>
#include <string>

test_plugin::test_plugin(r_vss::r_motion_event_plugin_host* host)
    : _host(host)
{
    R_LOG_INFO("test_plugin: Constructor called");
}

test_plugin::~test_plugin()
{
    R_LOG_INFO("test_plugin: Destructor called");
}

void test_plugin::stop()
{
    R_LOG_INFO("test_plugin: stop() called");
    // Nothing to do - test_plugin has no background threads
}

void test_plugin::post_motion_event(r_vss::r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height, const r_vss::motion_region& motion_bbox)
{
//    R_LOG_INFO("test_plugin: Received motion event %d for camera %s at ts %lld with frame data size %zu (resolution: %dx%d)",
//               evt, camera_id.c_str(), ts, frame_data.size(), width, height);

    // Verify frame data size matches expected BGR format (now using BGR instead of ARGB)
    size_t expected_size = width * height * 3; // BGR = 3 bytes per pixel
    if (frame_data.size() == expected_size)
    {
//        R_LOG_INFO("test_plugin: Frame data size matches expected BGR format");
        // Frame data can now be used directly without querying
    }
    // Removed warning log for size mismatch - just silently handle it
}

// Pure C API implementation - no C++ types in function signatures
extern "C"
{

R_API r_motion_plugin_handle load_plugin(r_motion_event_plugin_host_handle host)
{
    R_LOG_INFO("test_plugin: load_plugin() called");

    // Cast opaque handle back to actual type
    r_vss::r_motion_event_plugin_host* host_ptr = reinterpret_cast<r_vss::r_motion_event_plugin_host*>(host);

    // Create plugin instance
    test_plugin* plugin = new test_plugin(host_ptr);

    // Return as opaque handle
    return reinterpret_cast<r_motion_plugin_handle>(plugin);
}

R_API void stop_plugin(r_motion_plugin_handle plugin)
{
    R_LOG_INFO("test_plugin: stop_plugin() called");

    test_plugin* plugin_ptr = reinterpret_cast<test_plugin*>(plugin);
    plugin_ptr->stop();
}

R_API void destroy_plugin(r_motion_plugin_handle plugin)
{
    R_LOG_INFO("test_plugin: destroy_plugin() called");

    // Cast opaque handle back to actual type and delete
    test_plugin* plugin_ptr = reinterpret_cast<test_plugin*>(plugin);
    delete plugin_ptr;
}

R_API void post_motion_event(
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
    bool has_motion)
{
    // Cast opaque handle back to actual type
    test_plugin* plugin_ptr = reinterpret_cast<test_plugin*>(plugin);

    // Convert C types back to C++ types
    std::string camera_id_str(camera_id);
    std::vector<uint8_t> frame_data_vec(frame_data, frame_data + frame_data_size);
    r_vss::motion_region motion_bbox = {motion_x, motion_y, motion_width, motion_height, has_motion};
    r_vss::r_motion_event evt_enum = static_cast<r_vss::r_motion_event>(evt);

    // Call the C++ method
    plugin_ptr->post_motion_event(evt_enum, camera_id_str, ts, frame_data_vec, width, height, motion_bbox);
}

}
