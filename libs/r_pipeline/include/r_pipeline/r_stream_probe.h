#ifndef r_pipeline_r_stream_probe_h
#define r_pipeline_r_stream_probe_h

#include "r_pipeline/r_stream_info.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// What a camera's stream really is, as opposed to what ONVIF says it is: its SPS, its frame
// rate, how far apart its keyframes are and whether it uses B-slices. Used to check a substream before it's chosen.

namespace r_pipeline
{

struct r_stream_probe_result
{
    // The camera answered (its SDP arrived), and sent video.
    bool answered {false};
    bool received_video {false};

    r_encoding encoding {H264_ENCODING};
    // From the first SPS seen; 0 when none was.
    uint16_t width {0};
    uint16_t height {0};
    uint8_t profile_idc {0};
    uint8_t level_idc {0};
    uint8_t constraint_flags {0};  // H.264 only, as r_h264_sps holds them

    // Access units that held a picture, and how many of them were keyframes.
    int frames {0};
    int key_frames {0};
    // Frames per second over the pts the probe saw; 0 with fewer than two frames.
    double fps {0.0};
    // The longest gap between two keyframes, in ms; -1 with fewer than two keyframes.
    int64_t key_interval_ms {-1};
    // A B-slice was parsed, or a picture's pts went backwards (decode order isn't display order).
    bool b_slices {false};
};

// Counts what r_stream_probe_result needs from access units (Annex-B, as r_gst_source delivers them). The SPS and
// PPS come in band: r_gst_source's parsers repeat them before each keyframe. Slices before the first PPS can't be
// parsed, so a B-slice there is only caught by its pts.
class r_stream_tally final
{
public:
    R_API explicit r_stream_tally(r_encoding encoding);
    R_API ~r_stream_tally() noexcept;
    R_API r_stream_tally(const r_stream_tally&) = delete;
    R_API r_stream_tally& operator=(const r_stream_tally&) = delete;

    // One access unit; pts in ms.
    R_API void add(const uint8_t* au, size_t size, bool key, int64_t pts_ms);
    R_API r_stream_probe_result result() const;

private:
    struct state;
    std::unique_ptr<state> _s;
};

// Opens rtsp_url and tallies its video until it has seen two keyframes and at least `min_seconds`, or for at most
// `max_seconds`. Over TCP, then over UDP when the camera answers but sends no video over TCP, as the other probes do.
R_API r_stream_probe_result probe_stream(
    const std::string& rtsp_url,
    const r_utils::r_nullable<std::string>& username = r_utils::r_nullable<std::string>(),
    const r_utils::r_nullable<std::string>& password = r_utils::r_nullable<std::string>(),
    int min_seconds = 3,
    int max_seconds = 9
);

// A substream's limits.
constexpr int64_t SUBSTREAM_MAX_KEY_INTERVAL_MS = 4000;
constexpr double SUBSTREAM_MIN_FPS = 5.0;
// Aspects further apart than this (as a fraction of main's) make the substream anamorphic.
constexpr double SUBSTREAM_ANAMORPHIC_TOLERANCE = 0.02;

struct r_substream_check
{
    bool ok {false};
    // Why not, for the camera's settings ("none: <reason>"); empty when ok.
    std::string reason;
    // Its picture is squeezed against main's (its own aspect differs by more than the tolerance): consumers then
    // use main's aspect. Allowed; set whether or not it's ok.
    bool anamorphic {false};
};

// Whether a probed stream can be the substream of a main stream of main_width x main_height (its SPS size): it sent
// video with an SPS, at most a quarter of main's pixels, no B-slices, keyframes no further apart than
// SUBSTREAM_MAX_KEY_INTERVAL_MS and at least SUBSTREAM_MIN_FPS. The first failing reason is given.
R_API r_substream_check check_substream(const r_stream_probe_result& sub, uint16_t main_width, uint16_t main_height);

}

#endif
