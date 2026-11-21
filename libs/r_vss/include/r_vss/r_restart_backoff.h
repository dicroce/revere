#ifndef __revere_restart_backoff_h
#define __revere_restart_backoff_h

#include "r_utils/r_macro.h"
#include <chrono>
#include <string>

namespace r_vss
{

// How soon a camera's stream may start again after it died. One instance per
// camera, kept across reconnects.
//
// A stream that delivered video for a minute or more and then died starts again
// at once, as before: a camera that was recording should be back as soon as it
// can be, and that resets every wait below. One that streamed for less (a
// camera that sends a few frames and hangs up) starts again at once the first
// time, then waits 2 s, 4 s, 8 s and so on up to a minute, so it doesn't make a
// new connection every couple of seconds. A stream that died without a single
// frame waits:
//
// - The camera answered (its SDP came) but sent no video: 20 s, then 60 s,
//   then every 2 minutes. Some cameras keep a session for a while after the
//   client goes; one that's stuck gets room to clear rather than a new
//   connection every 20 seconds.
// - It didn't answer (refused, unreachable, the RTSP server stopped): 1 s, 2 s,
//   4 s, then every 5 s for the first 2 minutes of such failures in a row, so
//   a camera back from a short outage records again within seconds; after
//   that, every 2 minutes.
//
// A camera whose stream settings changed since (a fixed URL, say) starts at
// once, and counts from the start again.
class r_restart_backoff final
{
public:
    using clock = std::chrono::steady_clock;

    // A session that streamed at least this long resets every wait.
    static constexpr std::chrono::seconds LONG_SESSION {60};
    // The longest wait after sessions shorter than LONG_SESSION.
    static constexpr std::chrono::seconds SHORT_SESSION_MAX_WAIT {60};

    // A stream on this camera ended (it was found dead). `config` is the camera's
    // stream_config_hash as that stream had it.
    // `answered`: its SDP arrived (r_recording_context::answered()).
    // `streamed_for`: from its first video frame to its last (r_recording_context::streamed_for()).
    R_API void ended(bool received_video, bool answered, std::chrono::milliseconds streamed_for, clock::time_point now, const std::string& config);

    // Whether a new stream may start now, with the camera's settings as `config` has them.
    R_API bool may_start(clock::time_point now, const std::string& config) const { return config != _config || now >= _next_start; }

    // How long the current wait is (zero when there's none).
    R_API std::chrono::seconds wait() const { return _wait; }

    // Attempts in a row that ended without video.
    R_API int failures() const { return _failures; }

    // Sessions with video, but shorter than LONG_SESSION, since the last long one.
    R_API int short_sessions() const { return _short_sessions; }

private:
    int _failures {0};
    int _short_sessions {0};
    std::chrono::seconds _wait {0};
    clock::time_point _next_start {};
    // When this run of failures began.
    clock::time_point _first_failure {};
    std::string _config;
};

}

#endif
