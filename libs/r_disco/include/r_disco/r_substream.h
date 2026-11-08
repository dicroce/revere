#ifndef r_disco_r_substream_h
#define r_disco_r_substream_h

#include "r_disco/r_camera.h"
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

// A camera's main profile and the profiles that could be its substream, best first.
struct r_substream_candidate_list
{
    r_onvif::onvif_profile_info main;
    std::vector<r_onvif::onvif_profile_info> candidates;
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

// Main's picture size from its stream's codec ("H264" or "H265") and parameters (the SPS in them); 0 x 0 when they
// don't say.
R_API std::pair<uint16_t, uint16_t> main_picture_size(const std::string& codec, const std::string& parameters);

// Records a found substream on the camera: sub_mode "on", sub_status "ok", and the profile, address, codec and size
// the camera's settings show. codec, parameters and timebase are the substream's, from its SDP.
R_API void substream_set(
    r_camera& camera,
    const r_substream_choice& choice,
    const std::string& codec,
    const std::string& parameters,
    int timebase
);

// No substream: sub_mode "off", sub_status "none: <reason>", and the rest cleared. The camera uses its main stream.
R_API void substream_clear(r_camera& camera, const std::string& reason);

// After the camera's address changed: the substream's stream address, codec and codec parameters as the camera gives
// them now. When it is still the same stream (the same codec and picture size), the stored address and parameters
// are updated and this returns true. When the camera changed it, the substream goes back to "none: camera changed the
// stream" (substream_clear) and this returns false; the pick doesn't rerun by itself. A camera whose substream is off
// is left alone.
R_API bool substream_follow(r_camera& camera, const std::string& url, const std::string& codec, const std::string& parameters);

// Copies the substream fields (all sub_*) from one camera record to another.
R_API void substream_copy(const r_camera& from, r_camera& to);

}

#endif
