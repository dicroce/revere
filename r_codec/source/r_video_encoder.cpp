
#include "r_codec/r_video_encoder.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_std_utils.h"
#include <cstring>

using namespace r_codec;
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
    _codec(nullptr),
    _context(nullptr),
    _pts(0),
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
        int level
    ) :
    _codec_id(codec_id),
    _codec(avcodec_find_encoder(_codec_id)),
    _context(avcodec_alloc_context3(_codec)),
    _pts(0),
    _buffer(),
    _pkt()
{
    if(!_codec)
        R_THROW(("Failed to find codec"));
    if(!_context)
        R_THROW(("Failed to allocate context"));

    _context->codec_id = _codec_id;
    _context->codec_type = AVMEDIA_TYPE_VIDEO;
    _context->bit_rate = bit_rate;
    _context->width = w;
    _context->height = h;
    _context->time_base.num = framerate.den;
    _context->time_base.den = framerate.num;
    _context->framerate = framerate;
    _context->gop_size = gop_size;
    _context->max_b_frames = max_b_frames;
    _context->pix_fmt = pix_fmt;
    _context->profile = profile;
    _context->level = level;

    _context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    auto ret = avcodec_open2(_context, _codec, nullptr);
    if(ret < 0)
        R_THROW(("Failed to open codec: %s", _ff_rc_to_msg(ret).c_str()));
}

r_video_encoder::r_video_encoder(r_video_encoder&& obj) :
    _codec_id(std::move(obj._codec_id)),
    _codec(std::move(obj._codec)),
    _context(std::move(obj._context)),
    _pts(std::move(obj._pts)),
    _buffer(std::move(obj._buffer)),
    _pkt(std::move(obj._pkt))
{
    obj._codec_id = AV_CODEC_ID_NONE;
    obj._codec = nullptr;
    obj._context = nullptr;
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
        _codec = std::move(obj._codec);
        obj._codec = nullptr;
        _context = std::move(obj._context);
        obj._context = nullptr;
        _pts = std::move(obj._pts);
        _buffer = std::move(obj._buffer);
        _pkt = std::move(obj._pkt);
    }

    return *this;
}

void r_video_encoder::attach_buffer(const uint8_t* data, size_t size)
{
    _buffer.resize(size);
    memcpy(_buffer.data(), data, size);
}

r_codec_state r_video_encoder::encode()
{
    raii_ptr<AVFrame> frame(av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });

    auto res = av_image_fill_arrays(
        frame.get()->data,
        frame.get()->linesize,
        _buffer.data(),
        _context->pix_fmt,
        _context->width,
        _context->height,
        1
    );

    if(res < 0)
        R_THROW(("Failed to fill frame data: %s", _ff_rc_to_msg(res).c_str()));

    frame.get()->format = _context->pix_fmt;
    frame.get()->width = _context->width;
    frame.get()->height = _context->height;
    frame.get()->pts = _pts;
    ++_pts;

    _pkt = raii_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); });

    int sf_ret = avcodec_send_frame(_context, frame.get());

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

    auto rp_ret = avcodec_receive_packet(_context, _pkt.get());

    if(rp_ret == AVERROR(EAGAIN))
        return R_CODEC_STATE_AGAIN;
    else if(rp_ret == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else if(rp_ret < 0)
        R_THROW(("Failed to receive packet: %s", _ff_rc_to_msg(rp_ret).c_str()));

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
    if(_context)
    {
        avcodec_close(_context);
        av_free(_context);
        _context = nullptr;
    }

    // we don't allocate the codec we "find" it, so we don't free it
    if(_codec)
        _codec = nullptr;

    _buffer.clear();
    _pts = 0;
}
