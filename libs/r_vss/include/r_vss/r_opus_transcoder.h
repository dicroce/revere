#ifndef _r_vss_r_opus_transcoder_h
#define _r_vss_r_opus_transcoder_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace r_vss
{

// A camera's stored audio as 16 kHz mono Opus packets, for a system plugin: revere's own
// decode and Opus branch (restream_opus_audio_branch(), the one its <mount>/opus restream
// uses), in a GStreamer pipeline of the transcoder's own. Here in revere so a plugin
// needn't link GStreamer itself (r_system_plugin.h: a plugin links only revere's libraries).
//
//   - AAC (mpeg4-generic) is decoded first by r_av (restream_pcm()), with the
//     AudioSpecificConfig from the stored codec params; PCMU and PCMA go in as
//     stored, with the stored sample caps (encoded_audio_sample_caps).
//   - Other codecs, or a GStreamer without an element the branch needs, give
//     ok() false, with why_not() saying why.
//
// One thread at a time: a thread pushes stored frames and takes the Opus packets that
// have come out.
class r_opus_transcoder final
{
public:
    // codec_name and codec_params as storage keeps them. frame_ms: the Opus packets'
    // length, 20, 40 or 60 (anything else is taken as 20).
    R_API r_opus_transcoder(const std::string& codec_name, const std::string& codec_params, uint32_t frame_ms = 20);
    r_opus_transcoder(const r_opus_transcoder&) = delete;
    r_opus_transcoder& operator=(const r_opus_transcoder&) = delete;
    R_API ~r_opus_transcoder() noexcept;

    bool ok() const { return _ok; }
    const std::string& why_not() const { return _why_not; }
    uint32_t frame_ms() const { return _frame_ms; }

    // One stored audio frame, ts_ms its stored time.
    R_API void push(const uint8_t* data, size_t size, int64_t ts_ms);
    // The Opus packets out so far (each frame_ms() long, no RTP header), waiting up to
    // timeout_ms for the first.
    R_API std::vector<std::vector<uint8_t>> take(int timeout_ms = 0);

    struct impl;

private:
    std::unique_ptr<impl> _impl;
    bool _ok {false};
    std::string _why_not;
    uint32_t _frame_ms {20};
};

// An RTP packet's payload: after its header, CSRCs and any extension, before any
// padding. Empty if it isn't an RTP version 2 packet, or has no payload.
R_API std::vector<uint8_t> rtp_packet_payload(const uint8_t* packet, size_t size);

}

#endif
