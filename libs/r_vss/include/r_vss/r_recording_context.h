
#ifndef __r_vss_r_recording_context_h
#define __r_vss_r_recording_context_h

#include "r_vss/r_restream_track.h"
#include "r_vss/r_live_key_frame.h"
#include "r_vss/r_key_interval.h"
#include "r_vss/r_lag_watchdog.h"
#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_live_restream_host.h"
#include "r_vss/r_ws.h"
#include "r_disco/r_camera.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_sample_context.h"
#include "r_pipeline/r_gst_buffer.h"
#include "r_pipeline/r_stream_info.h"
#include "r_storage/r_storage_file.h"
#include "r_storage/r_md_storage_file.h"
#include "r_utils/r_blocking_q.h"
#include "r_utils/r_macro.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <thread>
#include <utility>
#include <deque>
#include <memory>
#include <set>
#include "r_cameras/r_camera_events.h"
#include "r_vss/r_motion_storage_sink.h"

struct _GstRTSPMediaFactory;
typedef _GstRTSPMediaFactory GstRTSPMediaFactory;

struct _GstRTSPMedia;
typedef _GstRTSPMedia GstRTSPMedia;

namespace r_vss
{

// Maximum frames to buffer for live restreaming before dropping old frames
// At 30fps, 300 frames = 10 seconds of buffer
constexpr size_t LIVE_RESTREAM_MAX_QUEUE_SIZE = 300;

// Maximum frames to buffer for playback restreaming
// Increased from 120 to 300 to accommodate 5-second fetches without dropping frames
// Video at 30fps: ~150 frames per 5 seconds
// Audio at 50fps: ~250 frames per 5 seconds
// 300 frame buffer ensures no drops for both streams
constexpr size_t PLAYBACK_RESTREAM_MAX_QUEUE_SIZE = 300;

// Safety cap for the per-camera video GOP cache used for fast-start of new
// live RTSP viewers. The cache is normally cleared on every keyframe, so it
// only grows beyond a typical GOP if a source stops producing keyframes.
constexpr size_t GOP_CACHE_MAX_FRAMES = 300;

// Live-edge catch-up. Transmission to a live viewer is paced at 1x by the
// outgoing timestamps, so any backlog that builds in that viewer's queue (a
// client stall, transient load, a storage-write hiccup upstream) would
// otherwise persist forever as a standing extra latency — and viewers that
// stalled differently end up seconds apart on the same camera. While a
// viewer's video queue depth is above the high water mark we compress its
// outgoing timestamps by CATCHUP_RATE (the client plays that much faster
// than real time, so the queue drains smoothly — no visible jump); the slew
// disengages at the low water mark. The same accumulated skew is applied to
// the session's audio timestamps so A/V stay in sync.
constexpr size_t LIVE_RESTREAM_CATCHUP_HIGH_WATER_FRAMES = 30;
constexpr size_t LIVE_RESTREAM_CATCHUP_LOW_WATER_FRAMES = 8;
constexpr double LIVE_RESTREAM_CATCHUP_RATE = 0.10;

class r_recording_context;

// Prefix for a camera's GStreamer element names (its pipeline, depayloaders,
// appsinks): the friendly name, or the camera id when it has none, then "_".
R_API std::string recording_source_name_prefix(const r_disco::r_camera& camera);

// The codec parameters a stream's video is stored with: the SDP's (sdp_codec_parameters), plus the source's caps
// (encoded_video_sample_caps), its frame rate, and the H.264 or H.265 parameter sets it saw.
R_API std::string stored_video_codec_parameters(const std::string& video_codec_name, const std::string& sdp_codec_parameters,
                                                r_pipeline::r_gst_source& source, const r_pipeline::sample_context& sc);

struct _frame_context
{
    uint64_t gst_pts;
    uint64_t gst_dts;
    bool key;
    r_pipeline::r_gst_buffer buffer;
};

// The need-data handler for a live restream appsrc: waits for the next frame
// on `q` and pushes it. It keeps waiting (in short slices) until a frame comes
// or the appsrc is being shut down. Returning without pushing would leave the
// appsrc waiting for a push that nothing makes (it asks for data again only
// after one), so a source that paused for a few seconds stalled the track for
// the rest of the session while its queue filled.
// audio: send DISCONT only where the track's times break (r_restream_track_gate::breaks); video buffers keep their flags.
R_API void restream_feed(GstElement* appsrc, r_utils::r_blocking_q<_frame_context>& q, r_restream_track_gate& gate, bool audio = false);

// One entry in the per-camera GOP cache. Stores the original source pts
// (in milliseconds, as delivered by the gst source) so the relative spacing
// can be reconstructed when bursting into a newly-connected viewer's queue.
// (The newest video key frame as received, for the detection pass's first look, is r_live_key_frame.)

struct _gop_cache_entry
{
    int64_t pts;
    bool key;
    r_pipeline::r_gst_buffer buffer;
    // The time storage gave the frame, set once write_frame() returns. A frame whose
    // write threw keeps the sentinel, and a feed's start at the newest key frame
    // (r_frame_feeds) isn't given a GOP with one.
    int64_t stored_ts {NOT_STORED};
    static constexpr int64_t NOT_STORED = INT64_MIN;
};

// The cache's newest frame (the one just written) gets the time storage gave it.
R_API void stamp_newest_gop_frame(std::deque<_gop_cache_entry>& cache, int64_t stored_ts);

// The cached GOP as a frame feed takes it, each frame mapped for reading into maps,
// which must outlive the result. A frame never stamped is marked not stored.
R_API std::vector<r_gop_frame> gop_for_feeds(const std::deque<_gop_cache_entry>& cache, std::vector<r_pipeline::r_gst_buffer::r_map_info>& maps);

struct live_restreaming_state
{
    r_recording_context* rc {nullptr};
    r_live_restream_host* sk {nullptr};  // For cleanup callback to safely access the map
    GstRTSPMedia* media {nullptr};
    std::string camera_id;  // To identify which camera this state belongs to

    GstElement* v_appsrc {nullptr};
    GstElement* a_appsrc {nullptr};
    // An Opus (/opus) session's AAC decoder: its appsrc is pcmsrc and takes the
    // audio as R_RESTREAM_PCM_CAPS (see restream_pcm). Null for every other session.
    std::shared_ptr<r_av::r_audio_decoder> pcm_decoder;
    // Names the session in logs: "<camera> session <n>".
    uint64_t session_id {0};
    std::string camera_name;
    // Whether each track's pipeline branch is asking for data; audio is only
    // queued while it is (see r_restream_track_gate).
    r_restream_track_gate video_gate;
    r_restream_track_gate audio_gate;
    bool audio_unread {false}; // source thread only: audio is being left out
    std::chrono::steady_clock::time_point created_at {std::chrono::steady_clock::now()};
    bool live_restream_key_sent {false};
    // Set true at LRS creation; the source thread drains the recording
    // context's GOP cache into this LRS on the next video frame, then clears
    // the flag. This lets a new viewer get frames immediately instead of
    // waiting for the source's next IDR.
    bool needs_gop_replay {true};
    bool first_restream_v_times_set {false};
    uint64_t first_restream_v_pts {0};
    uint64_t first_restream_v_dts {0};
    bool first_restream_a_times_set {false};
    uint64_t first_restream_a_pts {0};
    uint64_t first_restream_a_dts {0};
    // Live-edge catch-up state (see LIVE_RESTREAM_CATCHUP_* above). The bool
    // and last-pts fields are touched only by the video source thread;
    // catchup_skew_ns is also read by the audio thread, hence atomic. The skew
    // is subtracted from every outgoing pts/dts and only ever grows — it is a
    // permanent shift of the session's timeline, which is fine because only
    // deltas pace transmission.
    bool catching_up {false};
    bool last_catchup_v_pts_set {false};
    uint64_t last_catchup_v_pts_ns {0};
    std::atomic<uint64_t> catchup_skew_ns {0};
    // Bounded queues - drop oldest frames when full to prevent memory exhaustion
    r_utils::r_blocking_q<struct _frame_context> video_samples{LIVE_RESTREAM_MAX_QUEUE_SIZE};
    r_utils::r_blocking_q<struct _frame_context> audio_samples{LIVE_RESTREAM_MAX_QUEUE_SIZE};
};

// One camera stream's video to its live restream viewers: a cache of the newest GOP, which a viewer that connects is
// brought up to date from (a burst of compressed timestamps, so it plays them as fast as it can and doesn't stay a GOP
// behind), then each frame to every viewer's queue with the catch-up slew. Used by a recording context for the main
// stream and by a substream recorder for the smaller one. Called from the stream's source thread only.
class r_live_video_fanout final
{
public:
    // Calls fn on each viewer of the stream.
    typedef std::function<void(const std::function<void(live_restreaming_state&)>& fn)> for_each_viewer_t;

    // A frame as the source delivered it (pts in ms): into the cache, and to every viewer.
    R_API void post(int64_t pts, bool key, const r_pipeline::r_gst_buffer& buffer, const for_each_viewer_t& for_each_viewer);

    // Frames in the cache.
    R_API size_t cached_frames() const { return _gop_cache.size(); }

    // For frame feeds that start at the newest key frame (r_frame_feeds): the newest cached frame (the one just
    // written) gets the time storage gave it, and the cached GOP as a feed takes it (gop_for_feeds()).
    void stamp_newest(int64_t stored_ts) { stamp_newest_gop_frame(_gop_cache, stored_ts); }
    std::vector<r_gop_frame> gop_for_feeds(std::vector<r_pipeline::r_gst_buffer::r_map_info>& maps) const
    {
        return r_vss::gop_for_feeds(_gop_cache, maps);
    }

private:
    std::deque<_gop_cache_entry> _gop_cache;
};

// Sets up one viewer session of a video-only live restream mount (a substream's): its appsrc with `caps`, and its
// state, registered with the keeper under `key` (the camera's id and "/sub", so r_live_video_fanout finds it). `name`
// names the session in logs.
R_API void live_restream_configure_video_only(r_live_restream_host* sk, GstRTSPMedia* media, const std::string& key, const std::string& name, const r_pipeline::r_gst_caps& caps);

struct playback_restreaming_state
{
    playback_restreaming_state(const std::string& top_dir, r_disco::r_devices& devices)
        : top_dir(top_dir)
        , devices(devices)
        , video_samples(PLAYBACK_RESTREAM_MAX_QUEUE_SIZE)
        , audio_samples(PLAYBACK_RESTREAM_MAX_QUEUE_SIZE)
    {}

    r_recording_context* rc {nullptr};
    GstRTSPMedia* media {nullptr};
    std::string top_dir;
    r_disco::r_devices& devices;

    std::string friendly_name;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::chrono::system_clock::time_point query_start;
    std::chrono::system_clock::time_point query_end;
    std::string camera_id;
    r_utils::r_nullable<int64_t> first_ts;

    contents con;
    std::chrono::milliseconds playback_duration {0};

    bool has_audio {false};
    std::string video_codec_name;
    std::string video_codec_parameters;
    std::string audio_codec_name;
    std::string audio_codec_parameters;
    r_pipeline::r_encoding video_encoding;
    r_utils::r_nullable<r_pipeline::r_encoding> maybe_audio_encoding;
    // Each 5s fetch starts at the keyframe BEFORE its query start, so
    // consecutive chunks overlap; these track the newest posted ts per stream
    // so the overlap isn't re-posted (re-posting rewound the client's timeline
    // by up to a GOP at every chunk boundary).
    int64_t last_posted_v_ts {-1}; // stored ts are epoch ms, always positive
    int64_t last_posted_a_ts {-1};
    // Start of the recording segment holding (or following) start_time. The
    // first fetch's keyframe-before lookup can reach back across a gap into the
    // previous segment; frames older than this are dropped so the stream
    // doesn't open with that segment's tail followed by a gap-long pause.
    int64_t min_ts {0};
    // Bounded queues - drop oldest frames when full to prevent memory exhaustion
    r_utils::r_blocking_q<struct _frame_context> video_samples;
    r_utils::r_blocking_q<struct _frame_context> audio_samples;
    std::thread playback_thread;
    bool running;

    GstElement* v_appsrc {nullptr};
    GstElement* a_appsrc {nullptr};
};

class r_recording_context
{
public:
    R_API r_recording_context(r_stream_keeper* sk, const r_disco::r_camera& camera, const std::string& top_dir, r_ws& ws, uint32_t sim_mttf_seconds = 0, const std::string& rtsp_protocols = "TCP");
    R_API r_recording_context(const r_recording_context&) = delete;
    R_API r_recording_context(r_recording_context&&) = delete;
    R_API ~r_recording_context() noexcept;

    R_API r_recording_context& operator=(const r_recording_context&) = delete;
    R_API r_recording_context& operator=(r_recording_context&&) noexcept = delete;

    R_API bool dead() const;

    R_API r_disco::r_camera camera() const;

    R_API int32_t bytes_per_second() const;

    R_API bool receiving_video() const;
    // The path of the camera's live restream mount ("/<name>"), once it has one; empty before.
    R_API std::string restream_mount_path() const { std::lock_guard<std::mutex> g(_restream_mount_lock); return _restream_mount_path; }
    // Motion detection's key frames come from the camera's smaller stream, not from here (the stream keeper decides:
    // see r_motion_source_governor). This context then posts none to the motion engine.
    R_API void set_motion_from_sub(bool from_sub) { _motion_from_sub = from_sub; }
    R_API bool motion_from_sub() const { return _motion_from_sub; }
    // How far apart its key frames come (r_key_interval); negative until it has had two.
    R_API int64_t key_interval_ms() const { return _key_interval.interval_ms(); }
    // Milliseconds since the newest video frame arrived; a very large number when none has.
    R_API int64_t video_age_ms() const;

    // The camera answered: its SDP arrived, so it is reachable and speaks RTSP
    R_API bool answered() const { return _answered; }

    // The RTSP transport this attempt uses ("TCP" or "UDP")
    R_API const std::string& rtsp_protocols() const { return _rtsp_protocols; }

    // How far storage persistence trails frame reception, in ms: the newest
    // video frame received versus when the newest one written to storage was
    // received. ~0 when healthy; it's the queue's delay while frames wait
    // behind a slow disk (writes happen on the storage file's writer thread),
    // and grows steadily while writes fail. Reception-based signals like
    // receiving_video() and dead() can't see either, because the frames still
    // arrive from the camera.
    R_API int64_t storage_write_lag_ms() const;

    // How far the written video timeline trails the wall clock, in ms: now
    // minus the newest video timestamp actually written to storage. ~0 when
    // healthy. Grows when the recorded timeline detaches from real time (e.g.
    // a source timestamp discontinuity), a state in which writes succeed —
    // storage_write_lag_ms() stays 0 — but time-based queries find no recent
    // video.
    R_API int64_t storage_timeline_lag_ms() const;

    // How far this camera's frames reach us behind live (see r_lag_watchdog).
    // wants_reconnect: far enough behind, for long enough, that the stream
    // keeper should reconnect it to drop the backlog (it decides, with backoff).
    R_API r_lag_snapshot ingest_lag() const;

    R_API r_storage::r_md_storage_file& metadata_storage();

    R_API void write_metadata(const std::string& stream_tag, const std::string& json_data, int64_t timestamp_ms);

    R_API void live_restream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media);

    R_API void stop();

    // Asks its camera event subscription to stop (and unsubscribe) without
    // waiting: first thing at shutdown, before anything that can block.
    R_API void stop_camera_events();

    // The camera's ONVIF event subscription (shadow mode). Null status/empty
    // spans for a camera without one (added by RTSP URL).
    R_API bool has_camera_events() const;
    R_API r_cameras::r_camera_events_status camera_events_status() const;

    // The newest key frame received (null before the first).
    R_API r_utils::r_nullable<r_live_key_frame> latest_key_frame() const;

private:
    // Tiers 2 and 3: the camera's motion goes to the motion ring (.mdb) in
    // place of MOG2's: a run of 1s when an event ends, 0 each second between.
    void _on_camera_transition(const r_cameras::r_camera_transition& t);
    void _camera_motion_tick(int64_t ts);
    void _close_camera_motion_ring();
public:
    R_API std::vector<r_cameras::r_camera_span> camera_spans(int64_t since_ms) const;

    R_API void create_playback_mount(GstRTSPServer* server, GstRTSPMountPoints* mounts, const std::string& url, uint64_t start_ts, uint64_t end_ts);

private:
    std::tuple<std::string, std::chrono::system_clock::time_point, std::chrono::system_clock::time_point> _get_playback_url_parts();

    static void _live_restream_cleanup_cbs(live_restreaming_state* lrs);

    static void _playback_restream_media_configure_cbs(GstRTSPMediaFactory* factory, GstRTSPMedia* media, r_recording_context* rc);
    void _playback_restream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media);

    static void _playback_restream_cleanup_cbs(playback_restreaming_state* prs);

    r_stream_keeper* _sk;
    r_disco::r_camera _camera;
    std::string _top_dir;
    r_pipeline::r_gst_source _source;
    r_storage::r_storage_file _storage_file;
    std::unique_ptr<r_storage::r_md_storage_file> _md_storage_file;
    r_utils::r_nullable<r_storage::r_storage_write_context> _maybe_video_storage_write_context;
    r_utils::r_nullable<r_storage::r_storage_write_context> _maybe_audio_storage_write_context;
    std::chrono::system_clock::time_point _last_v_time;
    std::chrono::system_clock::time_point _last_a_time;
    std::chrono::system_clock::time_point _last_v_write_time; // creation: storage_write_lag_ms() before the first write
    bool _has_audio;
    std::chrono::system_clock::time_point _stream_start_ts;
    bool _stream_start_ts_set;
    uint64_t _v_bytes_received;
    uint64_t _a_bytes_received;
    std::map<std::string, r_pipeline::r_sdp_media> _sdp_medias;
    r_utils::r_nullable<r_pipeline::r_gst_caps> _video_caps;
    r_utils::r_nullable<r_pipeline::r_gst_caps> _audio_caps;
    mutable std::mutex _restream_mount_lock;
    std::string _restream_mount_path;
    std::mutex _playback_restreaming_states_lok;
    std::map<GstRTSPMedia*, std::shared_ptr<playback_restreaming_state>> _playback_restreaming_states;
    bool _got_first_audio_sample;
    std::atomic<bool> _got_first_video_sample;
    std::atomic<bool> _motion_from_sub {false};
    r_key_interval _key_interval;
    std::atomic<bool> _answered {false};
    std::string _rtsp_protocols;

    // Sliding-window bitrate tracking. Samples are (timestamp, cumulative bytes).
    // Appended at most once every 500ms from the packet receive paths; trimmed to
    // a 30s retention so bytes_per_second() can compute rate over the last ~10s.
    mutable std::mutex _byte_rate_mutex;
    // Fed from the video sample callback, read by the stream keeper.
    mutable std::mutex _lag_lok;
    r_lag_watchdog _lag;
    mutable std::deque<std::pair<std::chrono::system_clock::time_point, uint64_t>> _byte_rate_samples;

    void _record_byte_sample();
    // Feeds the lag watchdog one video frame and logs what it found; returns its lag, ms.
    int64_t _watch_lag(int64_t pts);
    bool _die;
    r_ws& _ws;
    // Per-camera rolling cache of video frames since the last keyframe.
    // Only touched by the source thread inside the video sample callback —
    // no mutex needed. Drained into a new live RTSP viewer's queue when
    // their LRS appears with needs_gop_replay set.
    r_live_video_fanout _video_fanout;
    std::unique_ptr<r_cameras::r_camera_events> _camera_events;

    std::mutex _camera_motion_lock;
    std::unique_ptr<r_ring_storage_sink> _camera_motion_ring;
    std::set<std::string> _camera_motion_sources;   // sources in motion now
    int64_t _camera_motion_start_ms {0};
    int64_t _camera_motion_first_ts {0};
    int64_t _camera_motion_last_s {-1};

    // A reference to the newest key frame (set on the GStreamer thread, read by
    // the detection pass).
    mutable std::mutex _latest_key_lock;
    bool _has_latest_key {false};
    int64_t _latest_key_ts {0};
    r_pipeline::r_gst_buffer _latest_key_buffer;
    std::string _latest_key_codec_name;
    std::string _latest_key_codec_parameters;
};

}

#endif
