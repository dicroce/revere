
#ifndef _r_system_plugin_h
#define _r_system_plugin_h

#include "r_utils/r_macro.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle
typedef void* r_system_plugin_handle;

// Required exports from each plugin shared library

// Get the plugin's unique identifier (GUID)
// This is called BEFORE load_system_plugin to check for duplicates
// Returns: A unique string identifier for this plugin (e.g., "com.example.myplugin")
// The returned string must remain valid for the lifetime of the loaded library
R_API const char* get_system_plugin_guid();

// Load and initialize the plugin
// Called once at Revere startup
// top_dir: Revere's top-level data directory
// logger_state: Pointer to host's r_logger::logger_state, plugin should call set_logger_state() with this
// Returns: Plugin handle (opaque pointer to plugin instance)
R_API r_system_plugin_handle load_system_plugin(const char* top_dir, void* logger_state);

// Start the plugin's operations
// Called after all plugins are loaded
R_API void start_system_plugin(r_system_plugin_handle plugin);

// Stop the plugin's operations
// Called during Revere shutdown
R_API void stop_system_plugin(r_system_plugin_handle plugin);

// Destroy and cleanup the plugin
// Called after stop, final cleanup before unloading
R_API void destroy_system_plugin(r_system_plugin_handle plugin);

// Check if the plugin is enabled
// Returns: true if enabled, false otherwise
R_API bool system_plugin_enabled(r_system_plugin_handle plugin);

// Enable or disable the plugin
// enabled: true to enable, false to disable
R_API void system_plugin_set_enabled(r_system_plugin_handle plugin, bool enabled);

// Optional: Get the plugin's current status string
// Returns: A status string (e.g., "disabled", "authenticating", "connected", "not_connected")
// The returned string must remain valid until the next call to this function
// Plugins that don't implement this export will have no status displayed
R_API const char* system_plugin_get_status(r_system_plugin_handle plugin);

// Optional: Get the plugin's own version (e.g. "0.9.2" or "0.9.2-4-gabc1234"),
// shown in Revere's About box and reported by its local API (/version).
// Called once after loading. The returned string must remain valid for the
// lifetime of the loaded library. Plugins without this export have no version.
R_API const char* get_system_plugin_version();

// Optional: Get additional status detail (e.g., user code, URL during auth)
// Returns: A detail string, empty if no details available
R_API const char* system_plugin_get_status_message(r_system_plugin_handle plugin);

// Optional: Receive a detection event emitted by a motion plugin
// Implement this if your system plugin wants to consume detection events.
// Boxes are in 640x640 letterbox space. A class the camera reported itself
// ("person", "vehicle", "animal") has no box and no confidence: x1, y1, x2, y2
// and confidence are all 0. Treat such a box as none (no area, no overlap),
// not as a point at the corner.
R_API void system_plugin_post_detection(r_system_plugin_handle plugin,
    const char* camera_id, int64_t ts, const char* class_name,
    float confidence, float x1, float y1, float x2, float y2);

// Optional: system_plugin_post_detection plus posted_at_ms, the gateway wall
// clock (epoch ms) when the detection was put on the detection bus. The host
// calls this instead of system_plugin_post_detection when the plugin exports
// it, so a plugin can export both and still load in an older host.
R_API void system_plugin_post_detection_v2(r_system_plugin_handle plugin,
    const char* camera_id, int64_t ts, const char* class_name,
    float confidence, float x1, float y1, float x2, float y2,
    int64_t posted_at_ms);

// Callback type the plugin invokes to deliver API results back to the host.
// name: identifies the payload (e.g. "camera_answers")
// json: the result payload as a JSON string
typedef void (*system_plugin_api_result_cb)(const char* name, const char* json, void* user_data);

// Optional: host calls this after loading the plugin to give it the callback.
// Implement this if your plugin needs to deliver results back to the host.
R_API void system_plugin_set_api_result_cb(r_system_plugin_handle plugin,
    system_plugin_api_result_cb cb, void* user_data);

// Optional: the host calls this when the set of cameras changes (a camera is
// added, removed or re-assigned) so the plugin can act immediately instead of
// waiting for its next periodic poll. Implement this if your plugin mirrors the
// camera/device list to an external service (e.g. the cloud device sync).
R_API void system_plugin_cameras_changed(r_system_plugin_handle plugin);

// Optional: run an action the user chose in the plugin's status message. A line
// {"text", "action": "<id>", "action_label": "<button>", "confirm": "<question>"?}
// draws a button under the text; choosing it (and confirming, if the line has
// a "confirm" question) calls this with the line's "action" id. Called on the
// UI thread, so return promptly and do any slow work on the plugin's own thread.
// Only a plugin that exports this should put "action" in its lines.
R_API void system_plugin_action(r_system_plugin_handle plugin, const char* action);

// ---- Live frames ----
// A plugin can read cameras' encoded frames as they're recorded. It pulls them on
// its own thread: the host never calls into the plugin with frames.

#define R_SYSTEM_PLUGIN_HOST_API_VERSION 2

typedef struct r_frame_subscription* r_frame_subscription_handle;

enum
{
    R_FRAME_VIDEO         = 1 << 0,
    R_FRAME_AUDIO         = 1 << 1,
    R_FRAME_KEY           = 1 << 2,   // a video key frame
    R_FRAME_CODEC_CHANGED = 1 << 3,   // the first frame of its media, or its codec name or params changed
    R_FRAME_GAP           = 1 << 4    // frames of this media were dropped before this one
};

// Flags for subscribe_frames_from (version 2).
enum
{
    // Start the feed's video at the camera's newest key frame, not its next: the frames
    // recorded since that key frame come first, then the live ones. Without a whole GOP
    // the host can give (its frames weren't all stored, or they don't fit the bound),
    // the feed starts at the next key frame, as without the flag.
    R_SUBSCRIBE_FROM_NEWEST_KEY = 1 << 0
};

typedef struct r_frame
{
    uint32_t struct_size;             // the plugin sets it to sizeof(r_frame) as it knows it; the host writes only fields that fit
    uint32_t flags;                   // R_FRAME_*
    int64_t ts;                       // the stored time, epoch ms
    const uint8_t* data;              // the encoded frame as stored
    size_t size;
    const char* codec_name;           // e.g. "h264", "aac"
    const char* codec_params;         // as storage keeps them
    const char* stream;               // "main" or "sub": the stream this frame came from
} r_frame;

typedef struct r_system_plugin_host_api
{
    uint32_t struct_size;
    uint32_t version;                 // R_SYSTEM_PLUGIN_HOST_API_VERSION
    void* host;                       // passed back as the first argument below

    // Starts a feed of one camera's frames. media is R_FRAME_VIDEO, R_FRAME_AUDIO or both.
    // stream is null or "main", or "sub" (served from main while the camera has no fresh
    // substream; each frame's stream says which). Queued up to max_queued_bytes: 0 is 4 MB,
    // and less than 1 MB is raised to 1 MB, so a key frame always fits. Null if the camera
    // isn't recording here, for other media bits or stream names, or past 16 feeds for the
    // plugin. One reader per feed.
    r_frame_subscription_handle (*subscribe_frames)(void* host, const char* camera_id, const char* stream, uint32_t media, size_t max_queued_bytes);

    // Waits up to timeout_ms for the next frame. 1: a frame, its data valid until the next
    // call on this feed or unsubscribe. 0: none in time. -1: the feed is closed (the camera
    // was removed or stopped recording here, or the plugin is stopping); unsubscribe it, and
    // subscribe again when system_plugin_cameras_changed says the camera is back.
    int (*next_frame)(void* host, r_frame_subscription_handle sub, int timeout_ms, r_frame* frame);

    // Ends the feed; a next_frame waiting on it returns -1. From any thread.
    void (*unsubscribe)(void* host, r_frame_subscription_handle sub);

    // Version 2. As subscribe_frames, with flags (R_SUBSCRIBE_*): null for a flag the host
    // doesn't know. A plugin checks version >= 2 before calling it.
    r_frame_subscription_handle (*subscribe_frames_from)(void* host, const char* camera_id, const char* stream, uint32_t media, size_t max_queued_bytes, uint32_t flags);
} r_system_plugin_host_api;

// Optional: the host calls this once after load_system_plugin, before start, with the
// functions above. The table stays valid until destroy_system_plugin returns. A plugin
// stopping unsubscribes its feeds; the host closes any it left.
R_API void system_plugin_set_host_api(r_system_plugin_handle plugin, const r_system_plugin_host_api* api);

// Convenience: call this to post a result — null-safe wrapper around the callback.
static inline void system_plugin_post_api_result(system_plugin_api_result_cb cb, void* user_data,
    const char* name, const char* json)
{
    if(cb)
        cb(name, json, user_data);
}

#ifdef __cplusplus
}
#endif

#endif
