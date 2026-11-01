
#ifndef __revere_frame_feeds_h
#define __revere_frame_feeds_h

#include "r_utils/r_macro.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace r_vss
{

// One encoded frame from a feed: as the camera's recording stored it, with its
// stored time.
struct r_feed_frame
{
    uint32_t flags {0};             // r_frame_feeds::VIDEO, AUDIO, KEY, CODEC_CHANGED, GAP
    int64_t ts {0};                 // stored time, epoch ms
    std::vector<uint8_t> data;
    std::string codec_name;
    std::string codec_params;
    std::string stream;             // "main" or "sub": the stream this frame came from
};

// Feeds of cameras' encoded frames, for readers that pull them on their own
// thread (plugins: see r_system_plugin.h). A reader subscribes to a camera's
// video, audio or both, and takes frames with next() as they come.
//
// Frames are posted from each camera's recording: post() costs one atomic load
// while nothing subscribes, and never waits on a reader. Each feed queues its
// frames up to its byte bound. A frame that doesn't fit empties the queue: video
// then waits for its next key frame, audio carries on, and the first frame of
// each after it is marked GAP. A feed's video starts at a key frame, and the
// first frame of each media, and any whose codec name or parameters changed,
// is marked CODEC_CHANGED.
//
// A feed for "sub" takes the camera's substream while it's fresh, and main
// otherwise; there are no substreams yet, so it's always served from main, and
// its audio always comes from main.
class r_frame_feeds final
{
public:
    enum : uint32_t
    {
        VIDEO         = 1 << 0,
        AUDIO         = 1 << 1,
        KEY           = 1 << 2,
        CODEC_CHANGED = 1 << 3,
        GAP           = 1 << 4
    };

    // A feed's queue holds at most this many bytes of frames unless it asks otherwise.
    static constexpr size_t DEFAULT_MAX_BYTES = 4 * 1024 * 1024;

    R_API r_frame_feeds() = default;
    r_frame_feeds(const r_frame_feeds&) = delete;
    r_frame_feeds& operator=(const r_frame_feeds&) = delete;

    // A new feed of the camera's frames: media is VIDEO, AUDIO or both, stream is
    // empty or "main" for the main stream, or "sub". 0 for any other media or
    // stream. max_bytes 0 is DEFAULT_MAX_BYTES. The caller checks the camera.
    R_API uint64_t subscribe(const std::string& camera_id, const std::string& stream, uint32_t media, size_t max_bytes);

    // The feed's next frame, waiting up to timeout_ms. 1: a frame. 0: none in time.
    // -1: the feed is closed (or unknown). One reader per feed.
    R_API int next(uint64_t feed, int timeout_ms, r_feed_frame& frame);

    // Ends the feed; a next() waiting on it returns -1. From any thread.
    R_API void unsubscribe(uint64_t feed);

    // Closes the camera's feeds (it was removed, or stopped recording here):
    // their next() returns -1 until they're unsubscribed.
    R_API void close_camera(const std::string& camera_id);

    // Closes every feed.
    R_API void close_all();

    // A frame the camera recorded. stream is the stream it came from.
    R_API void post_video(const std::string& camera_id, const std::string& stream, const uint8_t* data, size_t size, bool key, int64_t ts, const std::string& codec_name, const std::string& codec_params);
    R_API void post_audio(const std::string& camera_id, const uint8_t* data, size_t size, int64_t ts, const std::string& codec_name, const std::string& codec_params);

    // For tests: open feeds, posts that found any feed at all, a feed's queued bytes.
    R_API size_t feeds() const;
    R_API size_t posts_seen() const;
    R_API size_t queued_bytes(uint64_t feed) const;

private:
    struct _feed
    {
        std::string camera_id;
        std::string serving {"main"};   // the stream its video is taken from now
        uint32_t media {0};
        size_t max_bytes {DEFAULT_MAX_BYTES};
        std::deque<r_feed_frame> q;
        size_t bytes {0};
        bool need_key {true};
        bool video_gap {false};
        bool audio_gap {false};
        bool video_seen {false};
        bool audio_seen {false};
        std::string video_codec;        // name + params of the last video frame queued
        std::string audio_codec;
        bool closed {false};
        std::condition_variable cond;
    };

    void _push(_feed& f, uint32_t media, const std::string& stream, const uint8_t* data, size_t size, bool key, int64_t ts, const std::string& codec_name, const std::string& codec_params);

    mutable std::mutex _lock;
    std::map<uint64_t, std::shared_ptr<_feed>> _feeds;
    uint64_t _next_id {1};
    std::atomic<size_t> _count {0};
    std::atomic<size_t> _posts_seen {0};
};

}

#endif
