
#ifndef r_codec_r_audio_decoder_h
#define r_codec_r_audio_decoder_h

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/samplefmt.h>
}

#include "r_av/r_codec_state.h"
#include "r_utils/r_macro.h"
#include <vector>
#include <cstdint>
#include <map>
#include <memory>

namespace r_av
{

// Note: This model where we keep previously cached resampler states around and then clear
// them at the end might need to change. If an app changed sample rates constantly it would be effectively
// a memory leak (not technically one because at decoder cleanup they would be destroyed).
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
    R_API r_audio_decoder(AVCodecID codec_id, bool parse_input = false);
    R_API r_audio_decoder(const r_audio_decoder&) = delete;
    R_API r_audio_decoder(r_audio_decoder&& obj);
    R_API ~r_audio_decoder();

    R_API r_audio_decoder& operator=(const r_audio_decoder&) = delete;
    R_API r_audio_decoder& operator=(r_audio_decoder&& obj);

    R_API void set_extradata(const std::vector<uint8_t>& ed);

    R_API void attach_buffer(const uint8_t* data, size_t size);

    R_API r_codec_state decode();
    R_API r_codec_state flush();

    R_API std::shared_ptr<std::vector<uint8_t>> get(AVSampleFormat output_format, int output_sample_rate, int output_channels);

    R_API int input_sample_rate() const;
    R_API int input_channels() const;
    R_API AVSampleFormat input_format() const;

private:
    void _clear();

    AVCodecID _codec_id;
    const AVCodec* _codec;
    AVCodecContext* _context;
    AVCodecParserContext* _parser;
    bool _parse_input;
    const uint8_t* _buffer;
    size_t _buffer_size;
    const uint8_t* _pos;
    int _remaining_size;
    AVFrame* _frame;
    std::map<r_resampler_state, SwrContext*> _resamplers;
    bool _codec_opened;

    void _open_codec();
};

}

#endif
