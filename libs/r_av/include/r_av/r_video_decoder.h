
#ifndef r_codec_r_video_decoder_h
#define r_codec_r_video_decoder_h

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

#include "r_av/r_codec_state.h"
#include "r_av/r_hw_accel.h"
#include "r_utils/r_macro.h"
#include <vector>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

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

// Frames lost before they reached the decoder (see r_h264_frame_num_tracker).
struct r_decoder_gap_stats
{
    uint64_t gaps {0};              // times frame_num skipped
    uint64_t hidden_pictures {0};   // pictures not output while waiting for a keyframe
    uint64_t software_pictures {0}; // pictures decoded in software meanwhile
};

class r_video_decoder final
{
public:
    R_API r_video_decoder();
    R_API r_video_decoder(AVCodecID codec_id, r_hw_accel accel = r_hw_accel::none, bool parse_input = false);
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

    // Names this stream in the gap log lines (a camera id, say).
    R_API void set_log_tag(const std::string& tag);
    R_API r_decoder_gap_stats gap_stats() const;

private:
    void _clear();
    void _release_hw_frame();

    AVCodecID _codec_id;
    r_hw_accel _hw_accel;
    const AVCodec* _codec;
    AVCodecContext* _context;
    AVCodecParserContext* _parser;
    bool _parse_input;
    std::vector<uint8_t> _input;  // the attached data plus zeroed AV_INPUT_BUFFER_PADDING_SIZE
    const uint8_t* _buffer;       // into _input
    size_t _buffer_size;
    const uint8_t* _pos;
    int _remaining_size;
    AVFrame* _frame;
    AVFrame* _sw_frame;        // staging frame for hw->cpu transfer
    AVBufferRef* _hw_device_ctx;
    AVPixelFormat _hw_pix_fmt; // hw pixel format expected from decoder
    std::map<r_scaler_state, SwsContext*> _scalers;
    bool _codec_opened;

    void _open_codec();
    static AVPixelFormat _get_hw_format(AVCodecContext* ctx, const AVPixelFormat* pix_fmts);

    // H.264 on VideoToolbox: after lost frames, hide output until the next
    // IDR (or decode in software if none comes soon). See r_video_decoder.cpp.
    struct gap_guard;
    std::unique_ptr<gap_guard> _gaps;
    void _gap_after_send(const uint8_t* data, size_t size);
    bool _gap_take_output();
    r_codec_state _flush_receive();
};

}

#endif