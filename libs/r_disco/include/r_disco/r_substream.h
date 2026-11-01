#ifndef r_disco_r_substream_h
#define r_disco_r_substream_h

#include "r_onvif/r_onvif_session.h"
#include "r_pipeline/r_stream_probe.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace r_disco
{

// What looking for a camera's substream (its second, smaller stream, for live view and analysis) found.
struct r_substream_choice
{
    bool found {false};
    // Why there's none, for the camera's settings ("none:<reason>"); empty when found.
    std::string reason;

    // When found: the profile, its stream's URL as the camera gave it, what the probe saw, and whether its picture
    // is squeezed against main's (consumers then use main's aspect).
    r_onvif::onvif_profile_info profile;
    std::string rtsp_url;
    r_pipeline::r_stream_probe_result probe;
    bool anamorphic {false};
};

// How find_substream reaches the camera, so it runs against a real one (r_onvif_cam::get_stream_uri and
// r_pipeline::probe_stream with the camera's credentials) or a stand-in.
struct r_substream_camera
{
    // The RTSP URL of a profile's stream; throws, or returns "", when the camera won't give one.
    std::function<std::string(const r_onvif::onvif_profile_token&)> stream_uri;
    // Opens a URL and tallies its video (r_pipeline::probe_stream).
    std::function<r_pipeline::r_stream_probe_result(const std::string&)> probe;
};

// Up to this many candidates are probed; each probe can take up to 9 s on a camera that doesn't answer.
constexpr int SUBSTREAM_MAX_PROBES = 2;

// Finds main's substream among the camera's profiles: the candidates best first (r_onvif::substream_candidates),
// each one's stream address, a probe of it, and r_pipeline::check_substream against main's size (its SPS size, as
// recording sees it). The first that passes is the choice. When none does, the reason is the best candidate's, or
// "no smaller stream" when the camera has no candidate at all. Opens at most SUBSTREAM_MAX_PROBES streams, one at
// a time, and blocks while it probes, so run it off the UI thread.
R_API r_substream_choice find_substream(
    const std::vector<r_onvif::onvif_profile_info>& profiles,
    const r_onvif::onvif_profile_info& main,
    uint16_t main_width,
    uint16_t main_height,
    const r_substream_camera& camera
);

}

#endif
