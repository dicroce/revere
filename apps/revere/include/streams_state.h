#ifndef __revere_streams_state_h
#define __revere_streams_state_h

#include "r_disco/r_camera.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace revere
{

// The Streams section of a camera's properties: what the camera uses now, the
// smaller streams it could use instead, and what Save will write. Worker
// threads (asking the camera) write it while the dialog draws it, so all of it
// is under `m`.
struct streams_state
{
    struct candidate
    {
        std::string token;
        std::string title;  // e.g. "H.264 \xC2\xB7 640 \xC3\x97 480"
    };

    std::mutex m;
    // Bumped each time the dialog opens; a worker started for an earlier one drops its result.
    uint64_t generation {0};
    // The camera can be asked over ONVIF (a camera added by its RTSP address can't).
    bool onvif {false};
    // The recording (main) stream, e.g. "H.264 \xC2\xB7 2560 \xC3\x97 1920"; empty until known.
    std::string recording;
    // The list of smaller streams is coming / a check of the camera is running.
    bool loading {false};
    bool working {false};
    std::vector<candidate> candidates;
    // The substream fields as Save will write them.
    r_disco::r_camera sub;
    bool changed {false};
};

}

#endif
