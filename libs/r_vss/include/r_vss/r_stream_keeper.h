
#ifndef __r_vss_r_stream_keeper_h
#define __r_vss_r_stream_keeper_h

#include "r_vss/r_restream_track.h"
#include "r_vss/r_motion_engine.h"
#include "r_vss/r_motion_event_plugin_host.h"
#include "r_vss/r_system_plugin_host.h"
#include "r_vss/r_ws.h"
#include "r_vss/r_prune.h"
#include "r_vss/r_camera_stats.h"
#include "r_cameras/r_camera_events.h"
#include "r_vss/r_detection_pass.h"
#include "r_vss/r_tier_controller.h"
#include "r_vss/r_tier_outputs.h"
#include "r_vss/r_rtsp_transport.h"
#include <functional>
#include "r_disco/r_devices.h"
#include "r_disco/r_camera.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_work_q.h"
#include "r_utils/r_macro.h"
#include "r_pipeline/r_stream_info.h"
#include <map>
#include <set>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>
#include <functional>
#include <chrono>

#ifdef IS_WINDOWS
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif
#ifdef IS_LINUX
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#ifdef IS_LINUX
#pragma GCC diagnostic pop
#endif
#ifdef IS_WINDOWS
#pragma warning( pop )
#endif

struct _GMainLoop;
typedef struct _GMainLoop GMainLoop;
struct _GstRTSPServer;
typedef struct _GstRTSPServer GstRTSPServer;
struct _GstRTSPMountPoints;
typedef struct _GstRTSPMountPoints GstRTSPMountPoints;
struct _GstRTSPMediaFactory;
typedef struct _GstRTSPMediaFactory GstRTSPMediaFactory;
struct _GstRTSPMedia;
typedef _GstRTSPMedia GstRTSPMedia;

namespace r_vss
{

struct live_restreaming_state;

// Types of queue overflow that can occur
enum class r_overflow_type
{
    none = 0,
    live_restream = 1,      // Live RTSP restreaming queue overflow
    playback_restream = 2,  // Playback restreaming queue overflow
    motion_detection = 4    // Motion detection queue overflow
};

// Bitwise operators for r_overflow_type
inline r_overflow_type operator|(r_overflow_type a, r_overflow_type b) {
    return static_cast<r_overflow_type>(static_cast<int>(a) | static_cast<int>(b));
}
inline r_overflow_type operator&(r_overflow_type a, r_overflow_type b) {
    return static_cast<r_overflow_type>(static_cast<int>(a) & static_cast<int>(b));
}
inline r_overflow_type& operator|=(r_overflow_type& a, r_overflow_type b) {
    a = a | b;
    return a;
}
inline bool has_overflow(r_overflow_type flags, r_overflow_type check) {
    return static_cast<int>(flags & check) != 0;
}

struct r_stream_status
{
    r_disco::r_camera camera;
    uint32_t bytes_per_second;
    bool receiving_video {false};
    int64_t storage_write_lag_ms {0}; // how far storage persistence trails reception
    int64_t storage_timeline_lag_ms {0}; // how far the written timeline trails the wall clock
    r_overflow_type overflow_flags {r_overflow_type::none};
    size_t dropped_frames {0};  // Total frames dropped since last check

    // Set when the camera has an assigned configuration but the recording
    // context failed to construct (e.g. legacy nanots v1 file). The UI uses
    // these to show a meaningful error in place of "N/A".
    bool failed {false};
    std::string failure_message;
    int failure_attempt_count {0};

    // The camera's ONVIF event subscription, when it has one.
    bool has_camera_events {false};
    r_cameras::r_camera_events_status camera_events;

    // Where its motion and object classes come from.
    r_tier_status motion_tier;

    // The RTSP transport the recorder is using ("TCP" or "UDP"), and whether
    // that's UDP standing in for a TCP setting the camera refused.
    std::string rtsp_transport;
    bool rtsp_transport_fallback {false};
};

// Passes MOG2's events on to the motion plugins, and tells the stream keeper
// about each one (the tier controller compares them with the camera's own).
class r_motion_event_tee final : public r_motion_event_sink
{
public:
    using event_fn = std::function<void(r_motion_event evt, const std::string& camera_id, int64_t ts)>;

    r_motion_event_tee(r_motion_event_sink& next, event_fn on_event) :
        _next(next),
        _on_event(std::move(on_event))
    {
    }

    void post(r_motion_event evt, const std::string& camera_id, int64_t ts,
              const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height,
              const motion_region& motion_bbox) override
    {
        if(_on_event)
            _on_event(evt, camera_id, ts);
        _next.post(evt, camera_id, ts, frame_data, width, height, motion_bbox);
    }

    void post(r_motion_event evt, const std::string& camera_id, int64_t ts,
              const std::vector<uint8_t>& frame_data, uint16_t width, uint16_t height,
              const motion_region& motion_bbox,
              const std::vector<motion_region>& motion_regions) override
    {
        if(_on_event)
            _on_event(evt, camera_id, ts);
        _next.post(evt, camera_id, ts, frame_data, width, height, motion_bbox, motion_regions);
    }

private:
    r_motion_event_sink& _next;
    event_fn _on_event;
};

enum r_stream_keeper_commands
{
    R_SK_FETCH_STREAM_STATUS,
    R_SK_IS_RECORDING,
    R_SK_STOP,
    R_SK_SUSPEND,
    R_SK_RESUME,
    R_SK_CREATE_PLAYBACK_MOUNT
};

struct r_stream_keeper_cmd
{
    r_stream_keeper_commands cmd;
    std::string id;

    // for R_SK_ADD_PLAYBACK_RESTREAM_MOUNT
    std::string friendly_name;
    uint64_t start_ts;
    uint64_t end_ts;
    std::string url;
};

struct r_stream_keeper_result
{
    bool is_recording;
    std::vector<r_stream_status> stream_infos;
};

class r_recording_context;

class r_stream_keeper final
{
public:
    R_API r_stream_keeper(r_disco::r_devices& devices, r_disco::r_agent& agent, const std::string& top_dir);
    R_API ~r_stream_keeper() noexcept;

    R_API void start();
    R_API void stop();

    R_API std::vector<r_stream_status> fetch_stream_status();

    R_API bool is_recording(const std::string& id);

    // Camera-reported event spans since since_ms (ONVIF, shadow mode); empty
    // for a camera that isn't recording or has no subscription.
    R_API std::vector<r_cameras::r_camera_span> camera_spans(const std::string& camera_id, int64_t since_ms);

    // A camera's event subscription reporting a start or end (any thread).
    R_API void camera_transition(const std::string& camera_id, const r_cameras::r_camera_transition& t);

    // The detection pass's finished sessions for a camera.
    R_API std::vector<r_detection_session> detection_sessions(const std::string& camera_id, int64_t since_ms);

    // A camera's motion tier now (1 when unknown), and why.
    R_API int motion_tier(const std::string& camera_id);
    R_API r_tier_status motion_tier_status(const std::string& camera_id) const;

    R_API std::string add_restream_mount(const std::map<std::string, r_pipeline::r_sdp_media>& sdp_medias, const r_disco::r_camera& camera, r_recording_context* rc, r_utils::r_nullable<r_pipeline::r_encoding> video_encoding, r_utils::r_nullable<r_pipeline::r_encoding> audio_encoding);
    R_API void remove_restream_mount(const std::string& path);

    R_API void post_frame_to_motion_engine(r_pipeline::r_gst_buffer buffer, int64_t ts, const std::string& video_codec_name, const std::string& video_codec_params, const std::string& camera_id, bool is_key_frame);

    R_API std::vector<uint8_t> get_jpg(const std::string& camera_id, int64_t ts, uint16_t w, uint16_t h);

    R_API std::chrono::hours get_retention_hours(const std::string& camera_id);

    R_API void bounce(const std::string& camera_id);

    // Streams lost and restarted (dead contexts replaced), in memory since
    // revere started; see reconnect_log.
    R_API const reconnect_log& reconnects() const { return _reconnects; }

    R_API void suspend(const std::string& camera_id);
    R_API void resume(const std::string& camera_id);

    R_API std::string create_restream_launch_string(r_pipeline::r_encoding video_encoding, int video_format, r_utils::r_nullable<r_pipeline::r_encoding> maybe_audio_encoding, int audio_format);

    R_API r_disco::r_devices& get_devices();

    // System-password administration from the trusted local (desktop) admin UI.
    R_API void set_system_password(const std::string& password);
    R_API bool system_password_set() const;
    R_API void clear_system_password();

    R_API void write_metadata(const std::string& camera_id, const std::string& stream_tag, const std::string& json_data, int64_t timestamp_ms);

    R_API std::vector<std::string> get_loaded_system_plugins() const;
    R_API bool is_system_plugin_enabled(const std::string& plugin_name) const;
    R_API void set_system_plugin_enabled(const std::string& plugin_name, bool enabled);
    R_API std::string get_system_plugin_status(const std::string& plugin_name) const;
    R_API std::string get_system_plugin_status_message(const std::string& plugin_name) const;
    R_API std::string get_system_plugin_version(const std::string& plugin_name) const;

    // Live restreaming state management (owned by r_stream_keeper for safe cleanup)
    R_API void add_live_restreaming_state(GstRTSPMedia* media, std::shared_ptr<live_restreaming_state> lrs);
    R_API void remove_live_restreaming_state(GstRTSPMedia* media);
    R_API void iterate_live_restreaming_states(const std::string& camera_id, std::function<void(live_restreaming_state&)> fn);

    // Queue overflow monitoring
    R_API size_t get_motion_engine_dropped_count();
    R_API size_t get_motion_engine_queue_size() const;
    R_API r_overflow_type get_current_overflow_flags() const;
    R_API size_t get_total_dropped_frames() const;

    // Simulated failure for leak testing. 0 = disabled. Must be set before streams start.
    R_API void set_sim_mttf_seconds(uint32_t mttf) { _sim_mttf_seconds = mttf; }

    R_API void set_motion_tuning_fn(r_motion_tuning_fn fn);
    R_API void set_system_plugin_api_result_cb(std::function<void(const std::string&, const std::string&)> fn);

private:
    void _entry_point();
    void _rtsp_server_entry_point();
    std::vector<r_disco::r_camera> _get_current_cameras();
    void _add_recording_contexts(const std::vector<r_disco::r_camera>& cameras);
    // Takes the cameras' contexts out of _streams and returns them, for the
    // caller to destroy after releasing _streams_mutex.
    std::vector<std::shared_ptr<r_recording_context>> _remove_recording_contexts(const std::vector<r_disco::r_camera>& cameras);
    std::vector<r_stream_status> _fetch_stream_status() const;
    void _update_status_cache();
    void _update_retention_cache();
    static void _live_restream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media, gpointer user_data);
    void _stop(const std::string& id);
    static void _client_connected_cbs(GstRTSPServer* server, GstRTSPClient* client, r_stream_keeper* sk);
    static void _options_cbs(GstRTSPClient * client, GstRTSPContext * ctx, r_stream_keeper* sk);
    void _options_cb(GstRTSPClient * client, GstRTSPContext * ctx);
    void _create_playback_mount(const std::string& friendly_name, const std::string& url, uint64_t start_ts, uint64_t end_ts);

    r_disco::r_devices& _devices;
    r_disco::r_agent& _agent;
    std::string _top_dir;
    std::thread _th;
    uint32_t _sim_mttf_seconds {0};
    bool _running;
    mutable std::mutex _streams_mutex;  // Protects _streams and _suspended_cameras
    std::map<std::string, std::shared_ptr<r_recording_context>> _streams;
    std::set<std::string> _suspended_cameras;

    // Per-camera backoff state for cameras whose r_recording_context construction
    // throws (e.g. legacy nanots v1 .nts files that the v2 lib can't open).
    // Without this, the main loop retries every iteration with no sleep, flooding
    // logs and starving other work. Exponential backoff: 1s, 2s, 4s, ..., capped.
    // friendly_name + last_error are kept here so fetch_stream_status() can
    // surface failure info to the UI without re-resolving the camera object.
    struct _failed_camera_state
    {
        std::chrono::steady_clock::time_point next_retry_time;
        int attempt_count {0};
        std::string friendly_name;
        std::string last_error;
    };
    std::map<std::string, _failed_camera_state> _failed_cameras;
    // Per camera, kept across reconnects; protected by _streams_mutex
    std::map<std::string, r_rtsp_transport> _rtsp_transports;
    reconnect_log _reconnects;
    r_utils::r_work_q<r_stream_keeper_cmd, r_stream_keeper_result> _cmd_q;

    // Cached stream status for non-blocking reads from GUI thread
    mutable std::mutex _status_cache_mutex;
    std::vector<r_stream_status> _status_cache;

    // Cached retention hours for non-blocking reads from GUI thread (updated hourly)
    mutable std::mutex _retention_cache_mutex;
    std::map<std::string, std::chrono::hours> _retention_cache;
    std::chrono::steady_clock::time_point _last_retention_update;

    std::thread _rtsp_server_th;
    GMainLoop* _loop;
    GstRTSPServer* _server;
    GstRTSPMountPoints* _mounts;
    std::vector<GstRTSPMediaFactory*> _factories;
    r_motion_tuning_fn _motion_tuning_fn;
    r_motion_event_plugin_host _meph;
    r_motion_event_tee _motion_tee;
    r_motion_engine _motionEngine;
    r_system_plugin_host _system_plugin_host;
    r_ws _ws;
    r_prune _prune;

    // Live restreaming states - owned here so cleanup callback can safely access them
    // even after r_recording_context is destroyed
    mutable std::mutex _live_restreaming_states_lok;
    std::map<GstRTSPMedia*, std::shared_ptr<live_restreaming_state>> _live_restreaming_states;

    // Overflow tracking
    std::chrono::steady_clock::time_point _last_overflow_log_time;
    std::chrono::steady_clock::time_point _last_overflow_time;
    size_t _total_motion_dropped {0};
    size_t _total_restream_dropped {0};
    // Live restream drops per session since the last overflow warning.
    std::map<uint64_t, restream_overflow> _restream_overflow_since_log;
    std::chrono::steady_clock::time_point _restream_overflow_window_start {std::chrono::steady_clock::now()};
    r_overflow_type _current_overflow_flags {r_overflow_type::none};

    // Runs over tier 3 cameras' motion when a motion plugin offers object
    // detection. Kept (stopped) after stop() so late transitions are harmless.
    std::mutex _detection_pass_lock;
    std::unique_ptr<r_detection_pass> _detection_pass;

    // Motion tiers (milestone 5).
    void _on_mog2_event(r_motion_event evt, const std::string& camera_id, int64_t ts);
    static void _on_detection_bus(void* userdata, const r_detection& det);
    void _tier_tick();
    void _camera_class_output(const std::string& camera_id, const r_cameras::r_camera_transition& t, int64_t now_ms);
    void _pass_found(const std::string& camera_id, const std::vector<r_detection>& detections);
    void _pass_session(const r_detection_session& session);
    bool _motion_detection_on(const std::string& camera_id);
    std::string _camera_name(const std::string& camera_id);

    mutable std::mutex _tier_lock;
    r_tier_controller _tiers;
    std::map<std::string, int64_t> _mog2_starts;                       // camera id -> its MOG2 event's start
    std::map<std::string, int64_t> _camera_class_starts;               // camera id + "|" + class -> start (tier 2)
    std::map<std::string, std::vector<r_detection>> _emitted_first;    // camera id -> first found, already on the bus (tier 3)
    std::set<std::string> _tiers_restored;                             // cameras whose saved verification was given back
};

}

#endif
