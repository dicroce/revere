
#include "r_av/r_audio_encoder.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_std_utils.h"
#include <cstring>

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

r_audio_encoder::r_audio_encoder() :
    _codec_id(AV_CODEC_ID_NONE),
    _codec(nullptr),
    _context(nullptr),
    _pts(0),
    _frame_sent(false),
    _flushing(false),
    _buffer(),
    _pkt()
{
}

r_audio_encoder::r_audio_encoder(
        AVCodecID codec_id,
        uint32_t bit_rate,
        int sample_rate,
        int channels,
        AVSampleFormat sample_fmt
    ) :
    _codec_id(codec_id),
    _codec(avcodec_find_encoder(_codec_id)),
    _context(avcodec_alloc_context3(_codec)),
    _pts(0),
    _frame_sent(false),
    _flushing(false),
    _buffer(),
    _pkt()
{
    if(!_codec)
        R_THROW(("Failed to find audio codec"));
    if(!_context)
        R_THROW(("Failed to allocate audio codec context"));

    _context->codec_id = _codec_id;
    _context->codec_type = AVMEDIA_TYPE_AUDIO;
    _context->bit_rate = bit_rate;
    _context->sample_rate = sample_rate;
    _context->sample_fmt = sample_fmt;
    av_channel_layout_default(&_context->ch_layout, channels);
    _context->time_base = {1, sample_rate};

    _context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "strict", "experimental", 0);
    auto ret = avcodec_open2(_context, _codec, &opts);
    av_dict_free(&opts);

    if(ret < 0)
        R_THROW(("Failed to open audio codec: %s", _ff_rc_to_msg(ret).c_str()));
}

r_audio_encoder::r_audio_encoder(r_audio_encoder&& obj) :
    _codec_id(std::move(obj._codec_id)),
    _codec(std::move(obj._codec)),
    _context(std::move(obj._context)),
    _pts(std::move(obj._pts)),
    _frame_sent(std::move(obj._frame_sent)),
    _flushing(std::move(obj._flushing)),
    _buffer(std::move(obj._buffer)),
    _pkt(std::move(obj._pkt))
{
    obj._codec_id = AV_CODEC_ID_NONE;
    obj._codec = nullptr;
    obj._context = nullptr;
}

r_audio_encoder::~r_audio_encoder()
{
    _clear();
}

r_audio_encoder& r_audio_encoder::operator=(r_audio_encoder&& obj)
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
        _frame_sent = std::move(obj._frame_sent);
        _flushing = std::move(obj._flushing);
        _buffer = std::move(obj._buffer);
        _pkt = std::move(obj._pkt);
    }

    return *this;
}

int r_audio_encoder::get_frame_size() const
{
    if(!_context)
        R_THROW(("Context is not initialized"));
    return _context->frame_size;
}

void r_audio_encoder::attach_buffer(const uint8_t* data, size_t size, int64_t pts)
{
    _buffer.resize(size);
    memcpy(_buffer.data(), data, size);
    _pts = pts;
    _frame_sent = false;
    _flushing = false;
}

r_codec_state r_audio_encoder::encode()
{
    raii_ptr<AVFrame> frame(av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });

    frame.get()->nb_samples = _context->frame_size;
    frame.get()->format = _context->sample_fmt;
    av_channel_layout_copy(&frame.get()->ch_layout, &_context->ch_layout);
    frame.get()->pts = _pts;

    int ret = av_frame_get_buffer(frame.get(), 0);
    if(ret < 0)
        R_THROW(("Failed to allocate audio frame buffer: %s", _ff_rc_to_msg(ret).c_str()));

    bool is_planar = av_sample_fmt_is_planar(_context->sample_fmt);
    int nb_channels = _context->ch_layout.nb_channels;
    int bytes_per_sample = av_get_bytes_per_sample(_context->sample_fmt);
    int bytes_per_channel = _context->frame_size * bytes_per_sample;

    if(!is_planar)
    {
        // Packed: single contiguous buffer
        memcpy(frame.get()->data[0], _buffer.data(), _buffer.size());
    }
    else
    {
        // Planar: channels stored sequentially in _buffer, copy each plane
        for(int ch = 0; ch < nb_channels; ch++)
            memcpy(frame.get()->data[ch], _buffer.data() + ch * bytes_per_channel, bytes_per_channel);
    }

    _pkt = raii_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); });

    if(!_frame_sent)
    {
        int sf_ret = avcodec_send_frame(_context, frame.get());

        if(sf_ret == AVERROR(EAGAIN))
        {
            auto rp_ret = avcodec_receive_packet(_context, _pkt.get());

            if(rp_ret == AVERROR(EAGAIN))
                return R_CODEC_STATE_AGAIN;
            else if(rp_ret == AVERROR_EOF)
                return R_CODEC_STATE_EOF;
            else if(rp_ret < 0)
                R_THROW(("Failed to receive audio packet: %s", _ff_rc_to_msg(rp_ret).c_str()));

            return R_CODEC_STATE_HAS_OUTPUT;
        }
        else if(sf_ret == AVERROR_EOF)
            return R_CODEC_STATE_EOF;
        else if(sf_ret < 0)
            R_THROW(("Failed to send audio frame: %s", _ff_rc_to_msg(sf_ret).c_str()));

        _frame_sent = true;
    }

    auto rp_ret = avcodec_receive_packet(_context, _pkt.get());

    if(rp_ret == AVERROR(EAGAIN))
        return R_CODEC_STATE_AGAIN;
    else if(rp_ret == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else if(rp_ret < 0)
        R_THROW(("Failed to receive audio packet: %s", _ff_rc_to_msg(rp_ret).c_str()));

    return R_CODEC_STATE_HAS_OUTPUT;
}

r_codec_state r_audio_encoder::flush()
{
    // Send the drain (null) frame exactly once, then pull buffered packets across
    // repeated calls. Re-sending null each call returns AVERROR_EOF and aborts the
    // drain early, dropping the encoder's last buffered frame(s) — at a per-window
    // boundary that becomes an audible ~one-frame audio gap.
    if(!_flushing)
    {
        int ret = avcodec_send_frame(_context, nullptr);
        if(ret < 0 && ret != AVERROR_EOF && ret != AVERROR(EAGAIN))
            R_THROW(("Failed to flush audio encoder: %s", _ff_rc_to_msg(ret).c_str()));
        _flushing = true;
    }

    _pkt = raii_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* p) { av_packet_free(&p); });
    auto rp_ret = avcodec_receive_packet(_context, _pkt.get());

    if(rp_ret == AVERROR(EAGAIN) || rp_ret == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else if(rp_ret < 0)
        R_THROW(("Failed to flush audio encoder: %s", _ff_rc_to_msg(rp_ret).c_str()));

    return R_CODEC_STATE_HAS_OUTPUT;
}

r_packet_info r_audio_encoder::get()
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

vector<uint8_t> r_audio_encoder::get_extradata() const
{
    vector<uint8_t> ed;
    ed.resize(_context->extradata_size);
    memcpy(ed.data(), _context->extradata, _context->extradata_size);
    return ed;
}

void r_audio_encoder::_clear()
{
    if(_context)
    {
        avcodec_free_context(&_context);
        _context = nullptr;
    }

    // we don't allocate the codec we "find" it, so we don't free it
    if(_codec)
        _codec = nullptr;

    _buffer.clear();
    _pts = 0;
}
