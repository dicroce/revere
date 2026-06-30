
#ifndef r_av_r_audio_encoder_h
#define r_av_r_audio_encoder_h

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
}

#include "r_av/r_codec_state.h"
#include "r_av/r_video_encoder.h"
#include "r_utils/r_std_utils.h"
#include "r_utils/r_macro.h"

#include <vector>
#include <cstdint>

namespace r_av
{

class r_audio_encoder final
{
public:
    R_API r_audio_encoder();

    R_API r_audio_encoder(
        AVCodecID codec_id,
        uint32_t bit_rate,
        int sample_rate,
        int channels,
        AVSampleFormat sample_fmt
    );

    R_API r_audio_encoder(const r_audio_encoder&) = delete;
    R_API r_audio_encoder(r_audio_encoder&& obj);
    R_API ~r_audio_encoder();

    R_API r_audio_encoder& operator=(const r_audio_encoder&) = delete;
    R_API r_audio_encoder& operator=(r_audio_encoder&& obj);

    // Number of samples per channel expected in each attach_buffer() call.
    // Callers must provide frame_size * channels * av_get_bytes_per_sample(sample_fmt) bytes.
    R_API int get_frame_size() const;

    R_API void attach_buffer(const uint8_t* data, size_t size, int64_t pts);

    R_API r_codec_state encode();
    R_API r_codec_state flush();

    R_API r_packet_info get();

    R_API std::vector<uint8_t> get_extradata() const;

private:
    void _clear();

    AVCodecID _codec_id;
    const AVCodec* _codec;
    AVCodecContext* _context;
    int64_t _pts;
    bool _frame_sent;
    bool _flushing;
    std::vector<uint8_t> _buffer;
    r_utils::r_std_utils::raii_ptr<AVPacket> _pkt;
};

}

#endif
