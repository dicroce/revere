
#ifndef r_av_r_audio_decoder_h
#define r_av_r_audio_decoder_h

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
}

#include "r_av/r_codec_state.h"
#include "r_utils/r_macro.h"
#include <vector>
#include <cstdint>
#include <map>
#include <memory>

namespace r_av
{

struct r_resampler_state
{
    AVSampleFormat input_format;
    int input_sample_rate;
    int input_channels;
    AVSampleFormat output_format;
    int output_sample_rate;
    int output_channels;
};

R_API bool operator<(const r_resampler_state& lhs, const r_resampler_state& rhs);

class r_audio_decoder final
{
public:
    R_API r_audio_decoder();
    R_API r_audio_decoder(AVCodecID codec_id);
    R_API r_audio_decoder(const r_audio_decoder&) = delete;
    R_API r_audio_decoder(r_audio_decoder&& obj);
    R_API ~r_audio_decoder();

    R_API r_audio_decoder& operator=(const r_audio_decoder&) = delete;
    R_API r_audio_decoder& operator=(r_audio_decoder&& obj);

    R_API void set_extradata(const std::vector<uint8_t>& ed);

    // Required for raw PCM codecs (pcm_mulaw, pcm_alaw) which have no extradata.
    // Must be called before the first decode().
    R_API void set_pcm_params(int sample_rate, int channels);

    R_API void attach_buffer(const uint8_t* data, size_t size);

    R_API r_codec_state decode();
    R_API r_codec_state flush();

    // Returns decoded (and optionally resampled) audio samples.
    // For packed formats: a single interleaved buffer (all channels together).
    // For planar formats: per-channel planes stored sequentially in the buffer.
    R_API std::shared_ptr<std::vector<uint8_t>> get(AVSampleFormat output_format, int output_sample_rate, int output_channels);

    R_API int input_sample_rate() const;
    R_API int input_channels() const;
    R_API AVSampleFormat input_sample_format() const;

private:
    void _clear();
    void _open_codec();

    AVCodecID _codec_id;
    const AVCodec* _codec;
    AVCodecContext* _context;
    const uint8_t* _buffer;
    size_t _buffer_size;
    AVFrame* _frame;
    std::map<r_resampler_state, SwrContext*> _resamplers;
    bool _codec_opened;
};

}

#endif
