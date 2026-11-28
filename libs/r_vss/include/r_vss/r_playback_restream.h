#ifndef __r_vss_r_playback_restream_h
#define __r_vss_r_playback_restream_h

// Recorded video over RTSP: the mounts that play a camera's recording back, at
// <camera>_<start>_<end>. They read storage only, so they don't depend on the
// camera being connected: a camera that is offline (or between reconnects)
// plays its recording back like any other.

#include "r_vss/r_recording_context.h"
#include "r_vss/r_query.h"
#include "r_disco/r_camera.h"
#include "r_disco/r_devices.h"
#include "r_pipeline/r_stream_info.h"
#include "r_utils/r_blocking_q.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
R_THIRD_PARTY_HEADERS_BEGIN
#include <gst/rtsp-server/rtsp-server.h>
R_THIRD_PARTY_HEADERS_END
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace r_vss
{

// One viewer's playback session. Its media owns it (deleted when the media
// is): the thread that reads storage for it is joined before it goes, and holds
// no reference of its own.
struct playback_restreaming_state
{
    playback_restreaming_state(const std::string& top_dir, r_disco::r_devices& devices)
        : top_dir(top_dir)
        , devices(devices)
        , video_samples(PLAYBACK_RESTREAM_MAX_QUEUE_SIZE)
        , audio_samples(PLAYBACK_RESTREAM_MAX_QUEUE_SIZE)
    {}

    GstRTSPMedia* media {nullptr};
    // The mount the session came from (a ref on mounts, released at cleanup): removed with the session, if it is still
    // the one at url.
    GstRTSPMountPoints* mounts {nullptr};
    GstRTSPMediaFactory* factory {nullptr};
    std::string url;
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
    // Set before the thread starts; cleared by the thread at the range's end,
    // or by the media's cleanup to stop it.
    std::atomic<bool> running {false};

    GstElement* v_appsrc {nullptr};
    GstElement* a_appsrc {nullptr};
};

// Of `cameras`, the one a playback URL names: `name` is its friendly name as
// friendly_mount_path() writes it, less the leading "/".
R_API r_utils::r_nullable<r_disco::r_camera> playback_camera(const std::vector<r_disco::r_camera>& cameras, const std::string& name);

// Mounts `url` to play camera_id's recording from start_ts to end_ts (epoch
// ms), from storage under top_dir. False, with nothing mounted, when nothing is
// recorded in that range. Throws when the recording can't be read. The mount
// goes when a session played from it ends (each playback URL is mounted again by
// the next request for it: r_stream_keeper's OPTIONS handler).
R_API bool add_playback_mount(GstRTSPMountPoints* mounts, const std::string& url, const std::string& top_dir, r_disco::r_devices& devices,
                              const std::string& camera_id, uint64_t start_ts, uint64_t end_ts);

}

#endif
