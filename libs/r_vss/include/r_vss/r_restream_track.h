#ifndef __revere_restream_track_h
#define __revere_restream_track_h

#include "r_utils/r_macro.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace r_vss
{

// A live restream track (one viewer session's audio or video) that hasn't
// asked for data for this long isn't being read: its RTSP client didn't set it
// up, or its branch of the session's pipeline stopped.
constexpr std::chrono::seconds LIVE_RESTREAM_TRACK_IDLE {5};

// Tracks whether a session's track is being pulled by its pipeline, so the
// source thread can stop queueing frames nobody will send (they'd only fill the
// queue and be dropped). Written by the session's streaming thread, read by the
// camera's source thread.
class r_restream_track_gate final
{
public:
    // Around each need-data call.
    R_API void begin_pull(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    R_API void end_pull(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // True while a pull is waiting for data, or one ended within `idle`.
    // False before the first pull.
    R_API bool wanted(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(),
                      std::chrono::steady_clock::duration idle = LIVE_RESTREAM_TRACK_IDLE) const;

private:
    std::atomic<bool> _waiting {false};
    std::atomic<int64_t> _last_pull_ns {0}; // steady_clock; 0 = never
};

// One session's frames dropped since the last report.
struct restream_overflow
{
    std::string camera;      // friendly name, or id
    uint64_t session {0};
    size_t video_dropped {0};
    size_t audio_dropped {0};
};

// "Live restream overflow (frames dropped in the last 30 s): Front door session 3 audio 470; Garage session 5 video 12, audio 15"
// Sessions with nothing dropped are left out; empty when nothing was.
R_API std::string format_restream_overflow(const std::vector<restream_overflow>& sessions, int seconds);

}

#endif
