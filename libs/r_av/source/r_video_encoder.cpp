
#include "r_av/r_video_encoder.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_std_utils.h"
#include <cstring>

extern "C"
{
#include <libavutil/opt.h>
}

using namespace r_av;
using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace std;

static string _ff_rc_to_msg(int rc)
{
    char msg_buffer[1024];
    if(av_strerror(rc, msg_buffer, 1024) < 0)
        R_THROW(("Unknown ff return code."));
    return string(msg_buffer);
}

r_video_encoder::r_video_encoder() :
    _codec_id(AV_CODEC_ID_NONE),
    _hw_accel(r_hw_accel::none),
    _codec(nullptr),
    _context(nullptr),
    _input_pix_fmt(AV_PIX_FMT_NONE),
    _sws_ctx(nullptr),
    _pts(0),
    _frame_sent(false),
    _buffer(),
    _pkt()
{
}

r_video_encoder::r_video_encoder(
        AVCodecID codec_id,
        uint32_t bit_rate,
        uint16_t w,
        uint16_t h,
        AVRational framerate,
        AVPixelFormat pix_fmt,
        uint8_t max_b_frames,
        uint16_t gop_size,
        int profile,
        int level,
        const std::string& preset,
        const std::string& tune,
        r_hw_accel accel
    ) :
    _codec_id(codec_id),
    _hw_accel(accel),
    _codec(nullptr),
    _context(nullptr),
    _input_pix_fmt(pix_fmt),
    _sws_ctx(nullptr),
    _pts(0),
    _frame_sent(false),
    _buffer(),
    _pkt()
{
    if(_hw_accel != r_hw_accel::none)
    {
        const char* enc_name = r_hw_accel_encoder_name(_hw_accel, _codec_id);
        if(!enc_name)
            R_THROW(("No hw encoder name for this accel/codec combination"));
        _codec = avcodec_find_encoder_by_name(enc_name);
        if(!_codec)
            R_THROW(("Failed to find hw encoder: %s", enc_name));
        R_LOG_INFO("r_video_encoder: using %s encoder (%s)", enc_name, r_hw_accel_encoder_name(_hw_accel, _codec_id));
    }
    else
    {
        _codec = avcodec_find_encoder(_codec_id);
        if(!_codec)
            R_THROW(("Failed to find codec"));
        R_LOG_INFO("r_video_encoder: using software encoder");
    }

    _context = avcodec_alloc_context3(_codec);
    if(!_context)
        R_THROW(("Failed to allocate context"));

    AVPixelFormat enc_pix_fmt = (_hw_accel != r_hw_accel::none)
        ? r_hw_accel_encoder_pix_fmt(_hw_accel)
        : pix_fmt;

    _context->codec_id = _codec->id;
    _context->codec_type = AVMEDIA_TYPE_VIDEO;
    _context->bit_rate = bit_rate;
    _context->rc_max_rate = bit_rate;
    _context->rc_buffer_size = bit_rate; // 1-second buffer
    _context->width = w;
    _context->height = h;
    _context->time_base.num = framerate.den;
    _context->time_base.den = framerate.num;
    _context->framerate = framerate;
    _context->gop_size = gop_size;
    _context->keyint_min = gop_size;
    _context->max_b_frames = max_b_frames;
    _context->pix_fmt = enc_pix_fmt;
    _context->profile = profile;
    _context->level = level;
    _context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // hw encoders handle their own threading; preset/tune are sw-encoder concepts
    AVDictionary* opts = nullptr;
    if(_hw_accel == r_hw_accel::none)
    {
        if(!preset.empty())
            av_dict_set(&opts, "preset", preset.c_str(), 0);
        if(!tune.empty())
            av_dict_set(&opts, "tune", tune.c_str(), 0);
    }
    else
    {
        // Force VBR with a hard max-rate cap — without this QSV defaults to CQP and ignores bit_rate/rc_max_rate
        av_dict_set(&opts, "rc_mode", "VBR", 0);
    }

    auto ret = avcodec_open2(_context, _codec, &opts);
    av_dict_free(&opts);

    if(ret < 0)
        R_THROW(("Failed to open codec: %s", _ff_rc_to_msg(ret).c_str()));

    // set up pixel format conversion if the hw encoder needs a different format
    if(_hw_accel != r_hw_accel::none && enc_pix_fmt != _input_pix_fmt)
    {
        _sws_ctx = sws_getContext(
            w, h, _input_pix_fmt,
            w, h, enc_pix_fmt,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );
        if(!_sws_ctx)
            R_THROW(("Failed to create pixel format conversion context"));
    }
}

r_video_encoder::r_video_encoder(r_video_encoder&& obj) :
    _codec_id(std::move(obj._codec_id)),
    _hw_accel(std::move(obj._hw_accel)),
    _codec(std::move(obj._codec)),
    _context(std::move(obj._context)),
    _input_pix_fmt(std::move(obj._input_pix_fmt)),
    _sws_ctx(std::move(obj._sws_ctx)),
    _pts(std::move(obj._pts)),
    _buffer(std::move(obj._buffer)),
    _pkt(std::move(obj._pkt))
{
    obj._codec_id = AV_CODEC_ID_NONE;
    obj._hw_accel = r_hw_accel::none;
    obj._codec = nullptr;
    obj._context = nullptr;
    obj._input_pix_fmt = AV_PIX_FMT_NONE;
    obj._sws_ctx = nullptr;
}

r_video_encoder::~r_video_encoder()
{
    _clear();
}

r_video_encoder& r_video_encoder::operator=(r_video_encoder&& obj)
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
        _input_pix_fmt = std::move(obj._input_pix_fmt);
        obj._input_pix_fmt = AV_PIX_FMT_NONE;
        _sws_ctx = std::move(obj._sws_ctx);
        obj._sws_ctx = nullptr;
        _pts = std::move(obj._pts);
        _buffer = std::move(obj._buffer);
        _pkt = std::move(obj._pkt);
    }

    return *this;
}

void r_video_encoder::attach_buffer(const uint8_t* data, size_t size, int64_t pts)
{
    _buffer.resize(size);
    memcpy(_buffer.data(), data, size);
    _pts = pts;
    _frame_sent = false;
}

void r_video_encoder::set_bitrate(uint32_t bitrate)
{
    if(!_context)
        R_THROW(("Context is not initialized"));

    _context->bit_rate = bitrate;
    _context->rc_max_rate = bitrate;
    _context->rc_buffer_size = bitrate;

    if(_hw_accel == r_hw_accel::none)
        av_opt_set_int(_context->priv_data, "b", bitrate, 0);
}

r_codec_state r_video_encoder::encode()
{
    raii_ptr<AVFrame> src_frame(av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });

    auto res = av_image_fill_arrays(
        src_frame.get()->data,
        src_frame.get()->linesize,
        _buffer.data(),
        _input_pix_fmt,
        _context->width,
        _context->height,
        1
    );

    if(res < 0)
        R_THROW(("Failed to fill frame data: %s", _ff_rc_to_msg(res).c_str()));

    src_frame.get()->format = _input_pix_fmt;
    src_frame.get()->width = _context->width;
    src_frame.get()->height = _context->height;
    src_frame.get()->pts = _pts;

    raii_ptr<AVFrame> conv_frame;
    AVFrame* frame = src_frame.get();

    if(_sws_ctx)
    {
        conv_frame = raii_ptr<AVFrame>(av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });
        conv_frame.get()->format = _context->pix_fmt;
        conv_frame.get()->width  = _context->width;
        conv_frame.get()->height = _context->height;
        conv_frame.get()->pts    = _pts;

        if(av_frame_get_buffer(conv_frame.get(), 0) < 0)
            R_THROW(("Failed to allocate conversion frame buffer"));

        sws_scale(
            _sws_ctx,
            src_frame.get()->data, src_frame.get()->linesize,
            0, _context->height,
            conv_frame.get()->data, conv_frame.get()->linesize
        );

        frame = conv_frame.get();
    }

    _pkt = raii_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); });

    if(!_frame_sent)
    {
        int sf_ret = avcodec_send_frame(_context, frame);

        if(sf_ret == AVERROR(EAGAIN))
        {
            auto rp_ret = avcodec_receive_packet(_context, _pkt.get());

            if(rp_ret == AVERROR(EAGAIN))
                return R_CODEC_STATE_AGAIN;
            else if(rp_ret == AVERROR_EOF)
                return R_CODEC_STATE_EOF;
            else if(rp_ret < 0)
                R_THROW(("Failed to receive packet: %s", _ff_rc_to_msg(rp_ret).c_str()));

            return R_CODEC_STATE_HAS_OUTPUT;
        }
        else if(sf_ret == AVERROR_EOF)
            return R_CODEC_STATE_EOF;
        else if(sf_ret < 0)
            R_THROW(("Failed to send frame: %s", _ff_rc_to_msg(sf_ret).c_str()));
        
        _frame_sent = true;
    }

    auto rp_ret = avcodec_receive_packet(_context, _pkt.get());

    if(rp_ret == AVERROR(EAGAIN))
        return R_CODEC_STATE_AGAIN;
    else if(rp_ret == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else if(rp_ret < 0)
        R_THROW(("Failed to receive packet: %s", _ff_rc_to_msg(rp_ret).c_str()));

    return R_CODEC_STATE_HAS_OUTPUT;
}

r_codec_state r_video_encoder::flush()
{
    int ret = avcodec_send_frame(_context, nullptr);
    if(ret == AVERROR(EAGAIN))
    {
        _pkt = raii_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); });
        auto rp_ret = avcodec_receive_packet(_context, _pkt.get());

        if(rp_ret == AVERROR(EAGAIN) || rp_ret == AVERROR_EOF)
            return R_CODEC_STATE_EOF;
        else if(rp_ret < 0)
            R_THROW(("Failed to flush encoder: %s", _ff_rc_to_msg(rp_ret).c_str()));

        return R_CODEC_STATE_HAS_OUTPUT;
    }
    else if(ret == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else if(ret < 0)
        R_THROW(("Failed to flush encoder: %s", _ff_rc_to_msg(ret).c_str()));

    _pkt = raii_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); });
    auto rp_ret = avcodec_receive_packet(_context, _pkt.get());

    if(rp_ret == AVERROR(EAGAIN) || rp_ret == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else if(rp_ret < 0)
        R_THROW(("Failed to flush encoder: %s", _ff_rc_to_msg(rp_ret).c_str()));

    return R_CODEC_STATE_HAS_OUTPUT;
}

r_packet_info r_video_encoder::get()
{
    if(!_pkt)
        R_THROW(("No packet available"));

    r_packet_info pi;
    pi.data = _pkt.get()->data;
    pi.size = _pkt.get()->size;
    pi.pts = _pkt.get()->pts;
    pi.dts = _pkt.get()->dts;
    pi.duration = _pkt.get()->duration;
    pi.key = (_pkt.get()->flags & AV_PKT_FLAG_KEY) != 0;
    pi.time_base = _context->time_base;

    return pi;
}

vector<uint8_t> r_video_encoder::get_extradata() const
{
    vector<uint8_t> ed;
    ed.resize(_context->extradata_size);
    memcpy(ed.data(), _context->extradata, _context->extradata_size);
    return ed;
}

void r_video_encoder::_clear()
{
    if(_sws_ctx)
    {
        sws_freeContext(_sws_ctx);
        _sws_ctx = nullptr;
    }

    if(_context)
    {
        //avcodec_close(_context);
        avcodec_free_context(&_context);
        //av_free(_context);
        _context = nullptr;
    }

    // we don't allocate the codec we "find" it, so we don't free it
    if(_codec)
        _codec = nullptr;

    _buffer.clear();
    _pts = 0;
}
