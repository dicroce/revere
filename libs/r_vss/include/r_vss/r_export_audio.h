#ifndef __r_vss_r_export_audio_h
#define __r_vss_r_export_audio_h

#include "r_av/r_muxer.h"
#include "r_av/r_audio_decoder.h"
#include "r_av/r_audio_encoder.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace r_vss
{

// A camera's recorded audio in an export. When the file's container can hold the camera's codec, the packets are
// copied as they are. When it can't (G.711 mu-law or A-law in an .mp4, the usual camera audio), they are decoded and
// encoded as AAC at the same rate. When neither works, the export has no audio, and why_dropped() says why: the
// header write would otherwise fail and the export would write nothing.
class r_export_audio final
{
public:
    enum class mode { copy, aac, drop };

    // codec_name and codec_params as stored with the recording; file_name picks the container (by its extension).
    R_API r_export_audio(const std::string& codec_name, const std::string& codec_params, const std::string& file_name);
    R_API r_export_audio(const r_export_audio&) = delete;
    R_API r_export_audio& operator=(const r_export_audio&) = delete;
    R_API ~r_export_audio() noexcept;

    R_API mode what() const { return _mode; }
    R_API const std::string& why_dropped() const { return _why_dropped; }

    // Adds the audio stream (nothing when dropped). Before muxer.open().
    R_API void add_stream(r_av::r_muxer& muxer);

    // One stored audio packet, ts in milliseconds from the start of the export.
    R_API void write(r_av::r_muxer& muxer, const uint8_t* data, size_t size, int64_t ts_ms);

    // The AAC encoder's last samples. Before the muxer is finalized.
    R_API void flush(r_av::r_muxer& muxer);

private:
    std::unique_ptr<r_av::r_audio_encoder> _new_encoder() const;
    void _encode_pending(r_av::r_muxer& muxer, bool pad);
    void _write(r_av::r_muxer& muxer);

    mode _mode {mode::copy};
    std::string _why_dropped;
    AVCodecID _codec_id {AV_CODEC_ID_NONE};
    int _rate {0};
    int _channels {0};

    std::unique_ptr<r_av::r_audio_decoder> _decoder;
    std::unique_ptr<r_av::r_audio_encoder> _encoder;
    std::vector<std::vector<float>> _pending;   // decoded samples not yet encoded, per channel
    bool _anchored {false};
    int64_t _next_sample {0};                   // the next pending sample's time, in samples from the export's start
};

// Whether the container a file name's extension picks can hold codec_id: 1 yes, 0 no, -1 not known.
R_API int export_container_holds(const std::string& file_name, AVCodecID codec_id);

}

#endif
