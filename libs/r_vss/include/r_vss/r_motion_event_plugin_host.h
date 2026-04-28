
#ifndef __r_motion_event_plugin_host_h
#define __r_motion_event_plugin_host_h

#include "r_vss/r_motion_event_sink.h"
#include "r_vss/r_motion_plugin.h"
#include "r_utils/r_dynamic_library.h"
#include "r_utils/r_macro.h"
#include "r_disco/r_devices.h"

#include <list>
#include <map>
#include <mutex>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace r_vss
{

class r_stream_keeper;

struct r_detection
{
    std::string camera_id;
    int64_t     ts;
    std::string class_name;
    float       confidence;
    float       x1, y1, x2, y2;  // 640x640 letterbox space
};

typedef void (*r_detection_callback)(void* userdata, const r_detection& det);

class r_motion_event_plugin_host : public r_motion_event_sink
{
public:
    R_API r_motion_event_plugin_host(r_disco::r_devices& devices, const std::string& top_dir, r_stream_keeper& stream_keeper);
    R_API ~r_motion_event_plugin_host();

    // Stop all plugins - signals them to stop processing and waits for threads to finish
    // Must be called before gst_deinit() to ensure clean shutdown
    R_API void stop();

    R_API void post(r_motion_event evt, const std::string& camera_id, int64_t ts, const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height, const motion_region& motion_bbox) override;

    R_API void emit_detection(const r_detection& det);
    R_API void register_detection_consumer(r_detection_callback callback, void* userdata);

    r_disco::r_devices& get_devices() { return _devices; }
    const std::string& get_top_dir() const { return _top_dir; }
    r_stream_keeper& get_stream_keeper() { return _stream_keeper; }

private:
    struct plugin_info
    {
        std::unique_ptr<r_utils::r_dynamic_library> library;
        r_motion_plugin_handle plugin_handle;  // Changed to use C API handle
        void (*stop_func)(r_motion_plugin_handle);  // Function pointer to stop_plugin
        void (*destroy_func)(r_motion_plugin_handle);  // Function pointer to destroy_plugin
        void (*post_func)(r_motion_plugin_handle, int, const char*, int64_t, const uint8_t*, size_t, uint16_t, uint16_t, int, int, int, int, bool);  // Function pointer to post_motion_event
    };

    r_disco::r_devices& _devices;
    std::string _top_dir;
    r_stream_keeper& _stream_keeper;
    std::list<plugin_info> _plugins;
    std::vector<std::pair<r_detection_callback, void*>> _detection_consumers;
    std::mutex _detection_consumers_mutex;
};

}

#endif
