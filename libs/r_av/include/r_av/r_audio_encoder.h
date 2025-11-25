
#ifndef r_codec_r_audio_encoder_h
#define r_codec_r_audio_encoder_h

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
}

#include "r_av/r_codec_state.h"
#include "r_utils/r_std_utils.h"
#include "r_utils/r_macro.h"

#include <vector>
#include <cstdint>
#include <map>

namespace r_av
{

struct r_packet_info;

class r_audio_encoder final
{
public:
    R_API r_audio_encoder();

    R_API r_audio_encoder(
        AVCodecID codec_id,
        uint32_t bit_rate,
        int sample_rate,
        int channels,
        AVSampleFormat sample_fmt,
        const std::string& preset = "",
        const std::string& tune = ""
    );

    R_API r_audio_encoder(const r_audio_encoder&) = delete;

    R_API r_audio_encoder(r_audio_encoder&& obj);

    R_API ~r_audio_encoder();
    R_API r_audio_encoder& operator=(const r_audio_encoder&) = delete;
    R_API r_audio_encoder& operator=(r_audio_encoder&& obj);

    R_API void attach_buffer(const uint8_t* data, size_t size, int64_t pts);

    R_API void set_bitrate(uint32_t bitrate);

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
    std::vector<uint8_t> _buffer;
    r_utils::r_std_utils::raii_ptr<AVPacket> _pkt;
};

}

#endif
