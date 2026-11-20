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
// A stream that delivered video and then died starts again at once, as before:
// a camera that was recording should be back as soon as it can be. A stream
// that died without a single frame waits, and the first frame resets it:
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

    // A stream on this camera ended (it was found dead). `config` is the camera's
    // stream_config_hash as that stream had it.
    // `answered`: its SDP arrived (r_recording_context::answered()).
    R_API void ended(bool received_video, bool answered, clock::time_point now, const std::string& config);

    // Whether a new stream may start now, with the camera's settings as `config` has them.
    R_API bool may_start(clock::time_point now, const std::string& config) const { return config != _config || now >= _next_start; }

    // How long the current wait is (zero when there's none).
    R_API std::chrono::seconds wait() const { return _wait; }

    // Attempts in a row that ended without video.
    R_API int failures() const { return _failures; }

private:
    int _failures {0};
    std::chrono::seconds _wait {0};
    clock::time_point _next_start {};
    // When this run of failures began.
    clock::time_point _first_failure {};
    std::string _config;
};

}

#endif
