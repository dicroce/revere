
#include "r_av/r_video_decoder.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_std_utils.h"
#include <cstring>
#include <libavutil/hwcontext.h>

using namespace r_av;
using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace std;

static const uint8_t READ_PADDING = 32;

static const char* _accel_name(r_hw_accel accel)
{
    switch(accel)
    {
        case r_hw_accel::cuda:         return "cuda";
        case r_hw_accel::qsv:          return "qsv";
        case r_hw_accel::d3d11va:      return "d3d11va";
        case r_hw_accel::vaapi:        return "vaapi";
        case r_hw_accel::videotoolbox: return "videotoolbox";
        default:                       return "software";
    }
}

static string _ff_rc_to_msg(int rc)
{
    char msg_buffer[1024];
    if(av_strerror(rc, msg_buffer, 1024) < 0)
        R_THROW(("Unknown ff return code."));
    return string(msg_buffer);
}

bool r_av::operator<(const r_scaler_state& lhs, const r_scaler_state& rhs)
{
    if((lhs.input_format < rhs.input_format) ||
       ((lhs.input_format == rhs.input_format) && lhs.input_width < rhs.input_width) ||
       ((lhs.input_format == rhs.input_format) && (lhs.input_width == rhs.input_width) && lhs.input_height < rhs.input_height) ||
       ((lhs.input_format == rhs.input_format) && (lhs.input_width == rhs.input_width) && (lhs.input_height == rhs.input_height) && lhs.output_format < rhs.output_format) ||
       ((lhs.input_format == rhs.input_format) && (lhs.input_width == rhs.input_width) && (lhs.input_height == rhs.input_height) && (lhs.output_format == rhs.output_format) && lhs.output_width < rhs.output_width) ||
       ((lhs.input_format == rhs.input_format) && (lhs.input_width == rhs.input_width) && (lhs.input_height == rhs.input_height) && (lhs.output_format == rhs.output_format) && (lhs.output_width == rhs.output_width) && lhs.output_height < rhs.output_height))
        return true;
    return false;
}

r_video_decoder::r_video_decoder() :
    _codec_id(AV_CODEC_ID_NONE),
    _hw_accel(r_hw_accel::none),
    _codec(nullptr),
    _context(nullptr),
    _parser(nullptr),
    _parse_input(false),
    _buffer(nullptr),
    _buffer_size(0),
    _pos(nullptr),
    _remaining_size(0),
    _frame(nullptr),
    _sw_frame(nullptr),
    _hw_device_ctx(nullptr),
    _hw_pix_fmt(AV_PIX_FMT_NONE),
    _scalers(),
    _codec_opened(false)
{
}

r_video_decoder::r_video_decoder(AVCodecID codec_id, r_hw_accel accel, bool parse_input) :
    _codec_id(codec_id),
    _hw_accel(accel),
    _codec(avcodec_find_decoder(_codec_id)),
    _context(avcodec_alloc_context3(_codec)),
    _parser(av_parser_init(_codec_id)),
    _parse_input(parse_input),
    _buffer(nullptr),
    _buffer_size(0),
    _pos(nullptr),
    _remaining_size(0),
    _frame(av_frame_alloc()),
    _sw_frame(nullptr),
    _hw_device_ctx(nullptr),
    _hw_pix_fmt(AV_PIX_FMT_NONE),
    _scalers(),
    _codec_opened(false)
{
    if(!_codec)
        R_THROW(("Failed to find codec"));
    if(!_context)
        R_THROW(("Failed to allocate context"));
    if(!_frame)
        R_THROW(("Failed to allocate frame"));

    _context->extradata = nullptr;
    _context->extradata_size = 0;

    if(_hw_accel != r_hw_accel::none)
    {
        auto device_type = r_hw_accel_to_device_type(_hw_accel);

        _hw_pix_fmt = r_hw_accel_get_pix_fmt(_codec, device_type);
        if(_hw_pix_fmt == AV_PIX_FMT_NONE)
            R_THROW(("Codec does not support requested hw accel"));

        int ret = av_hwdevice_ctx_create(&_hw_device_ctx, device_type, nullptr, nullptr, 0);
        if(ret < 0)
            R_THROW(("Failed to create hw device context: %s", _ff_rc_to_msg(ret).c_str()));

        _sw_frame = av_frame_alloc();
        if(!_sw_frame)
            R_THROW(("Failed to allocate sw frame"));

        _context->thread_count = 1;  // hw handles parallelism
    }
    else
    {
        if(_codec->capabilities & AV_CODEC_CAP_FRAME_THREADS)
            _context->thread_type = FF_THREAD_FRAME;
        else if(_codec->capabilities & AV_CODEC_CAP_SLICE_THREADS)
            _context->thread_type = FF_THREAD_SLICE;
        else _context->thread_count = 1;
    }

    _context->flags |= AV_CODEC_FLAG_LOW_DELAY;
    _context->workaround_bugs = FF_BUG_AUTODETECT;

    R_LOG_INFO("r_video_decoder: using %s backend", _accel_name(_hw_accel));
}

r_video_decoder::r_video_decoder(r_video_decoder&& obj) :
    _codec_id(std::move(obj._codec_id)),
    _hw_accel(std::move(obj._hw_accel)),
    _codec(std::move(obj._codec)),
    _context(std::move(obj._context)),
    _parser(std::move(obj._parser)),
    _parse_input(std::move(obj._parse_input)),
    _buffer(std::move(obj._buffer)),
    _buffer_size(std::move(obj._buffer_size)),
    _pos(std::move(obj._pos)),
    _remaining_size(std::move(obj._remaining_size)),
    _frame(std::move(obj._frame)),
    _sw_frame(std::move(obj._sw_frame)),
    _hw_device_ctx(std::move(obj._hw_device_ctx)),
    _hw_pix_fmt(std::move(obj._hw_pix_fmt)),
    _scalers(std::move(obj._scalers)),
    _codec_opened(std::move(obj._codec_opened))
{
    obj._codec_id = AV_CODEC_ID_NONE;
    obj._hw_accel = r_hw_accel::none;
    obj._codec = nullptr;
    obj._context = nullptr;
    obj._parser = nullptr;
    obj._buffer = nullptr;
    obj._buffer_size = 0;
    obj._pos = nullptr;
    obj._frame = nullptr;
    obj._sw_frame = nullptr;
    obj._hw_device_ctx = nullptr;
    obj._hw_pix_fmt = AV_PIX_FMT_NONE;

    // _context->opaque points into obj's _hw_pix_fmt — redirect to ours
    if(_context && _hw_accel != r_hw_accel::none)
        _context->opaque = &_hw_pix_fmt;
}

r_video_decoder::~r_video_decoder()
{
    _clear();
}

r_video_decoder& r_video_decoder::operator=(r_video_decoder&& obj)
{
    if(this != &obj)
    {
        _clear();

        _codec_id = std::move(obj._codec_id);
        obj._codec_id = AV_CODEC_ID_NONE;
        _hw_accel = std::move(obj._hw_accel);
        obj._hw_accel = r_hw_accel::none;
        _codec = std::move(obj._codec);
        obj._codec = nullptr;
        _context = std::move(obj._context);
        obj._context = nullptr;
        _parser = std::move(obj._parser);
        obj._parser = nullptr;
        _parse_input = std::move(obj._parse_input);
        _buffer = std::move(obj._buffer);
        obj._buffer = nullptr;
        _buffer_size = std::move(obj._buffer_size);
        obj._buffer_size = 0;
        _pos = std::move(obj._pos);
        obj._pos = nullptr;
        _remaining_size = std::move(obj._remaining_size);
        _frame = std::move(obj._frame);
        obj._frame = nullptr;
        _sw_frame = std::move(obj._sw_frame);
        obj._sw_frame = nullptr;
        _hw_device_ctx = std::move(obj._hw_device_ctx);
        obj._hw_device_ctx = nullptr;
        _hw_pix_fmt = std::move(obj._hw_pix_fmt);
        obj._hw_pix_fmt = AV_PIX_FMT_NONE;
        _scalers = std::move(obj._scalers);
        _codec_opened = std::move(obj._codec_opened);

        // _context->opaque points into obj's _hw_pix_fmt — redirect to ours
        if(_context && _hw_accel != r_hw_accel::none)
            _context->opaque = &_hw_pix_fmt;
    }

    return *this;
}

void r_video_decoder::set_extradata(const vector<uint8_t>& ed)
{
    if(!_context)
        R_THROW(("Context is not initialized"));

    if(_context->extradata)
    {
        av_free(_context->extradata);
        _context->extradata = nullptr;
        _context->extradata_size = 0;
    }

    _context->extradata = (uint8_t*)av_malloc(ed.size() + READ_PADDING);
    _context->extradata_size = (int)ed.size();
    memcpy(_context->extradata, ed.data(), ed.size());
}

AVPixelFormat r_video_decoder::_get_hw_format(AVCodecContext* ctx, const AVPixelFormat* pix_fmts)
{
    auto* target = static_cast<AVPixelFormat*>(ctx->opaque);
    for(const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p)
        if(*p == *target)
            return *p;
    return AV_PIX_FMT_NONE;
}

void r_video_decoder::_open_codec()
{
    if(_codec_opened)
        return;

    if(_hw_accel != r_hw_accel::none)
    {
        _context->hw_device_ctx = av_buffer_ref(_hw_device_ctx);
        _context->get_format    = _get_hw_format;
        _context->opaque        = &_hw_pix_fmt;
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "strict", "experimental", 0);
    int ret = avcodec_open2(_context, _codec, &opts);
    av_dict_free(&opts);
    if(ret < 0)
        R_THROW(("Failed to open codec: %s", _ff_rc_to_msg(ret).c_str()));

    _codec_opened = true;
}

void r_video_decoder::attach_buffer(const uint8_t* data, size_t size)
{
    _buffer = data;
    _buffer_size = size;
    _pos = _buffer;
    _remaining_size = (int)_buffer_size;
}

r_codec_state r_video_decoder::decode()
{
    _open_codec();

    auto packet = shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* pkt) { av_packet_free(&pkt); });

    if(_parse_input && _parser)
    {
        // Use the parser to split Annex B stream into proper access units
        uint8_t* out_data = nullptr;
        int out_size = 0;

        int consumed = av_parser_parse2(
            _parser,
            _context,
            &out_data,
            &out_size,
            _pos,
            _remaining_size,
            AV_NOPTS_VALUE,
            AV_NOPTS_VALUE,
            0
        );

        if(consumed < 0)
        {
            R_LOG_ERROR("Parser error");
            return R_CODEC_STATE_HUNGRY;
        }

        _pos += consumed;
        _remaining_size -= consumed;

        if(out_size == 0)
        {
            // Parser needs more data or hasn't output a frame yet
            if(_remaining_size > 0)
                return decode(); // Recursively try to parse more
            return R_CODEC_STATE_HUNGRY;
        }

        packet->data = out_data;
        packet->size = out_size;
    }
    else
    {
        // Direct mode - send buffer as-is
        packet->data = const_cast<uint8_t*>(_pos);
        packet->size = _remaining_size;
    }

    // Send the packet to the decoder
    int send_result = avcodec_send_packet(_context, packet.get());

    if (send_result >= 0) {
        if(!_parse_input)
        {
            // In direct mode, entire packet was consumed
            _pos += _remaining_size;
            _remaining_size = 0;
        }

        // Try to receive a frame
        int recv_result = avcodec_receive_frame(_context, _frame);

        if(recv_result >= 0)
        {
            if(_hw_accel != r_hw_accel::none && _frame->format == _hw_pix_fmt)
            {
                int tr = av_hwframe_transfer_data(_sw_frame, _frame, 0);
                if(tr < 0)
                    R_THROW(("Failed to transfer hw frame to cpu: %s", _ff_rc_to_msg(tr).c_str()));
                av_frame_unref(_frame);
                av_frame_move_ref(_frame, _sw_frame);
            }
            return R_CODEC_STATE_HAS_OUTPUT;
        }
        else if (recv_result == AVERROR_EOF)
            return R_CODEC_STATE_EOF;
        else if (recv_result == AVERROR(EAGAIN))
        {
            // Decoder needs more data - if parsing, try to feed more
            if(_parse_input && _remaining_size > 0)
                return decode();
            return R_CODEC_STATE_HUNGRY;
        }
        else {
            R_LOG_ERROR("Failed to receive frame: %s", _ff_rc_to_msg(recv_result).c_str());
            return R_CODEC_STATE_HUNGRY;
        }
    }
    else if (send_result == AVERROR(EAGAIN)) {
        // Decoder buffers full, try to get a frame first
        int recv_result = avcodec_receive_frame(_context, _frame);

        if (recv_result >= 0)
            return R_CODEC_STATE_AGAIN_HAS_OUTPUT;
        else
            return R_CODEC_STATE_HUNGRY;
    }
    else if (send_result == AVERROR_EOF) {
        return R_CODEC_STATE_EOF;
    }
    else {
        R_LOG_ERROR("Failed to send packet: %s", _ff_rc_to_msg(send_result).c_str());
        return R_CODEC_STATE_HUNGRY;
    }
}

r_codec_state r_video_decoder::flush()
{
    _open_codec();

    int ret = avcodec_send_packet(_context, nullptr);
    if(ret == AVERROR(EAGAIN))
    {
        auto rf_ret = avcodec_receive_frame(_context, _frame);

        if(rf_ret == AVERROR(EAGAIN) || rf_ret == AVERROR_EOF)
            return R_CODEC_STATE_EOF;
        else if(rf_ret < 0)
            R_THROW(("Failed to flush decoder: %s", _ff_rc_to_msg(rf_ret).c_str()));

        return R_CODEC_STATE_HAS_OUTPUT;
    }
    else if(ret == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else if(ret < 0)
        R_THROW(("Failed to flush decoder: %s", _ff_rc_to_msg(ret).c_str()));

    auto rf_ret = avcodec_receive_frame(_context, _frame);
    if(rf_ret == AVERROR(EAGAIN) || rf_ret == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else if(rf_ret < 0)
        R_THROW(("Failed to flush decoder: %s", _ff_rc_to_msg(rf_ret).c_str()));

    return R_CODEC_STATE_HAS_OUTPUT;
}

shared_ptr<vector<uint8_t>> r_video_decoder::get(AVPixelFormat output_format, uint16_t output_width, uint16_t output_height, int alignment)
{
    // Use frame properties directly: in the hw path _context->pix_fmt is the
    // hw pixel format, but after transfer _frame holds the sw format.
    r_scaler_state state;
    state.input_format = (AVPixelFormat)_frame->format;
    state.input_width = (uint16_t)_frame->width;
    state.input_height = (uint16_t)_frame->height;
    state.output_format = output_format;
    state.output_width = output_width;
    state.output_height = output_height;

    auto found = _scalers.find(state);

    if(found == end(_scalers))
    {
        _scalers[state] = sws_getContext(
            _frame->width,
            _frame->height,
            (AVPixelFormat)_frame->format,
            output_width,
            output_height,
            output_format,
            SWS_BILINEAR,
            NULL,
            NULL,
            NULL
        );
    }

    auto output_image_size = av_image_get_buffer_size(output_format, output_width, output_height, alignment);

    auto result = make_shared<vector<uint8_t>>(output_image_size);

    uint8_t* fields[AV_NUM_DATA_POINTERS];
    int linesizes[AV_NUM_DATA_POINTERS];

    auto ret = av_image_fill_arrays(fields, linesizes, result->data(), output_format, output_width, output_height, alignment);

    if(ret < 0)
        R_THROW(("Failed to fill arrays for picture: %s", _ff_rc_to_msg(ret).c_str()));

    ret = sws_scale(_scalers[state],
                    _frame->data,
                    _frame->linesize,
                    0,
                    _frame->height,
                    fields,
                    linesizes);

    if(ret < 0)
        R_THROW(("sws_scale() failed: %s", _ff_rc_to_msg(ret).c_str()));

    return result;
}

uint16_t r_video_decoder::input_width() const
{
    return (uint16_t)_context->width;
}

uint16_t r_video_decoder::input_height() const
{
    return (uint16_t)_context->height;
}

void r_video_decoder::_clear()
{
    for(auto s : _scalers)
        sws_freeContext(s.second);
    _scalers.clear();

    if(_sw_frame)
    {
        av_frame_free(&_sw_frame);
        _sw_frame = nullptr;
    }
    if(_frame)
    {
        av_frame_free(&_frame);
        _frame = nullptr;
    }
    if(_parser)
    {
        av_parser_close(_parser);
        _parser = nullptr;
    }
    if(_context)
    {
        if(_context->extradata)
        {
            av_free(_context->extradata);
            _context->extradata = nullptr;
            _context->extradata_size = 0;
        }
        avcodec_free_context(&_context);
        _context = nullptr;
    }
    if(_hw_device_ctx)
    {
        av_buffer_unref(&_hw_device_ctx);
        _hw_device_ctx = nullptr;
    }
}
