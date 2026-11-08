#ifndef __r_vss_r_substream_recorder_h
#define __r_vss_r_substream_recorder_h

#include "r_disco/r_camera.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_stream_info.h"
#include "r_vss/r_frame_feeds.h"
#include "r_storage/r_storage_file.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

struct _GstRTSPMediaFactory;
struct _GstRTSPMedia;

namespace r_vss
{

class r_live_restream_host;
class r_live_video_fanout;

// The substream ring: a small storage file of its own beside the camera's recording, kept only for live view.
// 13 blocks of 10 MiB, about 17 minutes at 1 Mbit/s.
constexpr size_t SUBSTREAM_RING_BLOCK_SIZE = 10485760;
constexpr size_t SUBSTREAM_RING_BLOCKS = 13;

// The substream counts as fresh while its newest frame is this recent.
constexpr int64_t SUBSTREAM_FRESH_MS = 5000;
// With no video for this long, the recorder is dead and is started again (as main's recording context).
constexpr int64_t SUBSTREAM_DEAD_MS = 20000;

// The substream ring's path for a camera recording to record_file_path: "front_door.nts" records its substream to
// "front_door_sub.nts" in the same directory.
R_API std::string substream_ring_path(const std::string& record_file_path);

// The path of a camera's substream restream mount, next to its main one ("/<name>", spaces as underscores):
// "/<name>/sub". It is only mounted while the camera's substream recorder is up, and a request for it at any other
// time can reach the main stream (the server takes the longest mount that is a prefix of the path), so consumers
// look at /cameras' sub_mount and sub_fresh first, and check the stream's size.
R_API std::string substream_mount_path(const r_disco::r_camera& camera);

// The path a viewer plays for a camera: the smaller stream's mount for a tile in a grid when revere says it is up
// and fresh (sub_mount and sub_fresh from /cameras), otherwise the main stream's. main_mount is /cameras' `mount`;
// an older revere doesn't send it, and the main path is then the label with its spaces as underscores. The single
// view and anything that isn't a small grid tile play main: they show enough of the picture to want it.
R_API std::string view_mount_path(const std::string& label, const std::string& main_mount, const std::string& sub_mount, bool sub_fresh, bool grid_tile);

// What r_stream_keeper does about a camera's substream recorder on a pass.
enum class substream_step { none, start, stop };

// wanted: the camera has a usable substream chosen (sub_mode on, sub_status ok, a URL, a recording path).
// main_receiving: its main recording is getting video, which comes first on a camera that limits sessions.
// running / running_dead: a recorder exists, and whether it's dead. backoff_over: long enough since the last start.
// A running recorder stops when it's dead or no longer wanted, or when main isn't receiving; one starts when
// wanted, main is receiving and the backoff is over.
R_API substream_step substream_next_step(bool wanted, bool main_receiving, bool running, bool running_dead, bool backoff_over);

// A substream is started again at most this often.
constexpr int64_t SUBSTREAM_RESTART_BACKOFF_MS = 30000;
// After this many recorders in a row ended without a single frame (the camera refused the stream, at DESCRIBE or at
// SETUP), or this many times main's recording was dropped soon after one started (a camera that closes its older
// session), the camera is taken to allow one stream at a time and the substream is given up.
constexpr int SUBSTREAM_REFUSALS_TO_GIVE_UP = 2;
// Main's recording is taken to have been cut by a substream recorder when its newest video frame arrived no sooner
// than SUBSTREAM_MAIN_DROP_SLACK_MS before the recorder started and less than SUBSTREAM_MAIN_DROP_WINDOW_MS after.
// (It isn't noticed until main has had no video for 20 s, so the time it is noticed says nothing.)
constexpr int64_t SUBSTREAM_MAIN_DROP_WINDOW_MS = 10000;
constexpr int64_t SUBSTREAM_MAIN_DROP_SLACK_MS = 2000;
// What the camera's settings say when it's given up (r_disco::substream_clear adds "none: ").
constexpr const char* SUBSTREAM_REFUSED_REASON = "camera allows one stream at a time";

// What the stream keeper knows about one camera's substream on a pass.
struct r_substream_pass
{
    // The recording context's copy of the camera has a usable substream chosen (sub_mode on, sub_status ok, a URL,
    // a recording path). That copy is as old as the context: see r_substream_governor.
    bool wanted {false};
    // The recording context that copy belongs to.
    std::shared_ptr<const void> context;
    // Main's recording is getting video, which comes first on a camera that limits sessions.
    bool main_receiving {false};
    // A recorder exists, whether it is dead, and how many frames it wrote.
    bool running {false};
    bool dead {false};
    int64_t frames_written {0};
    // The recorder's ring isn't where the camera's recording file now says it should be (it was moved).
    bool ring_moved {false};
    // Milliseconds since main's newest video frame (r_recording_context::video_age_ms).
    int64_t main_video_age_ms {0};
    int64_t now_ms {0};
};

struct r_substream_decision
{
    substream_step step {substream_step::none};
    // The camera was just taken to allow one stream at a time: the caller saves that (substream_clear with
    // SUBSTREAM_REFUSED_REASON) and the substream isn't started again until the camera's context is replaced.
    bool gave_up {false};
};

// Decides what to do about each camera's substream recorder, and remembers what it has seen: when each last started,
// how many recorders in a row never got a frame, how often main dropped soon after a start, and the cameras given
// up. A give-up is saved to the camera, but the stream keeper's copy of it only changes with its recording context
// (a settings Save replaces that), so a given-up camera is held off for as long as that same context lives; the
// next context is built from the saved camera. Keeper thread only; no I/O.
class r_substream_governor final
{
public:
    R_API r_substream_decision pass(const std::string& camera_id, const r_substream_pass& in);

    // Main's recording for the camera ended (a reconnect), noticed at now_ms, whose newest video frame was
    // main_video_age_ms old. True when its video stopped soon after a substream recorder started, again: the camera
    // closes its older session, which would cut gaps into main's recording. The caller saves the give-up before
    // main's context is built again.
    R_API bool main_dropped(const std::string& camera_id, int64_t now_ms, int64_t main_video_age_ms);

    // The camera is no longer recording here, or was suspended.
    R_API void forget(const std::string& camera_id);

private:
    struct state
    {
        bool started {false};
        int64_t started_ms {0};
        int refusals {0};
        int main_drops {0};
        std::weak_ptr<const void> given_up_context;
        bool given_up {false};
    };
    std::map<std::string, state> _cameras;
};

// Allocates the ring at ring_path unless a file is already there.
R_API void ensure_substream_ring(const std::string& ring_path);

// A camera's substream (r_camera::sub_*), recorded into its ring. It has its own RTSP session and storage file and
// records video only, so whatever happens to it, main's recording carries on. It starts on construction and stops on
// destruction; r_stream_keeper replaces it when it's dead.
class r_substream_recorder final
{
public:
    // feeds: where the frames also go, as the camera's "sub" stream (r_frame_feeds); null for none.
    // host: where its restream mount is added and its viewers are found (null for none: it only records).
    R_API r_substream_recorder(const r_disco::r_camera& camera, const std::string& ring_path, const std::string& rtsp_protocols, r_frame_feeds* feeds = nullptr, r_live_restream_host* host = nullptr);
    R_API ~r_substream_recorder() noexcept;
    R_API r_substream_recorder(const r_substream_recorder&) = delete;
    R_API r_substream_recorder& operator=(const r_substream_recorder&) = delete;

    // The camera answered (its SDP arrived).
    R_API bool answered() const { return _answered; }
    // A frame reached the ring within SUBSTREAM_FRESH_MS.
    R_API bool fresh() const;
    // No video for SUBSTREAM_DEAD_MS: since it started, or since its last frame.
    R_API bool dead() const;
    // Frames written to the ring so far.
    R_API int64_t frames_written() const { return _frames_written; }
    // Where it writes.
    R_API const std::string& ring_path() const { return _ring_path; }
    // The path of its live restream mount once it has one (it has video caps from its first frame); empty before.
    R_API std::string mount_path() const;

    // One viewer connected to its mount (the keeper's media-configure callback calls this).
    R_API void live_restream_media_configure(_GstRTSPMediaFactory* factory, _GstRTSPMedia* media);

private:
    int64_t _ms_since_video() const;

    r_disco::r_camera _camera;
    std::string _ring_path;
    r_frame_feeds* _feeds;
    r_live_restream_host* _sk;
    std::unique_ptr<r_live_video_fanout> _fanout;
    mutable std::mutex _mount_lock;
    std::string _mount_path;
    r_utils::r_nullable<r_pipeline::r_gst_caps> _video_caps;
    r_storage::r_storage_file _ring;
    r_utils::r_nullable<r_storage::r_storage_write_context> _write_context;
    std::mutex _sdp_lock;
    std::map<std::string, r_pipeline::r_sdp_media> _sdp_medias;
    std::atomic<bool> _answered {false};
    std::atomic<int64_t> _frames_written {0};
    // steady_clock ms of the newest frame written, or of the start before there is one.
    std::atomic<int64_t> _last_video_ms {0};
    // Last, so it's destroyed first: its callbacks stop before the write context and ring go.
    r_pipeline::r_gst_source _source;
};

}

#endif
