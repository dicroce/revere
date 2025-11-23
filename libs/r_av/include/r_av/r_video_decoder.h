
#ifndef r_codec_r_video_decoder_h
#define r_codec_r_video_decoder_h

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

#include "r_av/r_codec_state.h"
#include "r_utils/r_macro.h"
#include <vector>
#include <cstdint>
#include <map>
#include <memory>

namespace r_av
{

// Note: This model where we keep previously caches scaler states around and then clear
// them at the end might need to change. If an app resized constantly it would be effectively
// a memory leak (not technically one because at decoder cleanup they would be destroyed).
struct r_scaler_state
{
    AVPixelFormat input_format;
    uint16_t input_width;
    uint16_t input_height;
    AVPixelFormat output_format;
    uint16_t output_width;
    uint16_t output_height;
};

R_API bool operator<(const r_scaler_state& lhs, const r_scaler_state& rhs);

class r_video_decoder final
{
public:
    R_API r_video_decoder();
    R_API r_video_decoder(AVCodecID codec_id, bool parse_input = false);
    R_API r_video_decoder(const r_video_decoder&) = delete;
    R_API r_video_decoder(r_video_decoder&& obj);
    R_API ~r_video_decoder();

    R_API r_video_decoder& operator=(const r_video_decoder&) = delete;
    R_API r_video_decoder& operator=(r_video_decoder&& obj);

    R_API void set_extradata(const std::vector<uint8_t>& ed);

    R_API void attach_buffer(const uint8_t* data, size_t size);

    R_API r_codec_state decode();
    R_API r_codec_state flush();

    R_API std::shared_ptr<std::vector<uint8_t>> get(AVPixelFormat output_format, uint16_t output_width, uint16_t output_height, int alignment = 32);

    R_API uint16_t input_width() const;
    R_API uint16_t input_height() const;

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
    std::map<r_scaler_state, SwsContext*> _scalers;
    bool _codec_opened;

    void _open_codec();
};

}

#endif