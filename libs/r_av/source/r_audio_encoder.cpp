
#include "r_av/r_audio_encoder.h"
#include "r_av/r_video_encoder.h"
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
    _buffer(),
    _pkt()
{
}

r_audio_encoder::r_audio_encoder(
        AVCodecID codec_id,
        uint32_t bit_rate,
        int sample_rate,
        int channels,
        AVSampleFormat sample_fmt,
        const std::string& preset,
        const std::string& tune
    ) :
    _codec_id(codec_id),
    _codec(avcodec_find_encoder(_codec_id)),
    _context(avcodec_alloc_context3(_codec)),
    _pts(0),
    _frame_sent(false),
    _buffer(),
    _pkt()
{
    if(!_codec)
        R_THROW(("Failed to find codec"));
    if(!_context)
        R_THROW(("Failed to allocate context"));

    _context->codec_id = _codec_id;
    _context->codec_type = AVMEDIA_TYPE_AUDIO;
    _context->bit_rate = bit_rate;
    _context->sample_rate = sample_rate;
    _context->sample_fmt = sample_fmt;

    // Set up channel layout
    av_channel_layout_default(&_context->ch_layout, channels);

    _context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // Apply preset and tune options if specified
    AVDictionary* opts = nullptr;
    if(!preset.empty())
        av_dict_set(&opts, "preset", preset.c_str(), 0);
    if(!tune.empty())
        av_dict_set(&opts, "tune", tune.c_str(), 0);

    auto ret = avcodec_open2(_context, _codec, &opts);
    av_dict_free(&opts);

    if(ret < 0)
        R_THROW(("Failed to open codec: %s", _ff_rc_to_msg(ret).c_str()));
}

r_audio_encoder::r_audio_encoder(r_audio_encoder&& obj) :
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
        _buffer = std::move(obj._buffer);
        _pkt = std::move(obj._pkt);
    }

    return *this;
}

void r_audio_encoder::attach_buffer(const uint8_t* data, size_t size, int64_t pts)
{
    _buffer.resize(size);
    memcpy(_buffer.data(), data, size);
    _pts = pts;
    _frame_sent = false;
}

void r_audio_encoder::set_bitrate(uint32_t bitrate)
{
    if(!_context)
        R_THROW(("Context is not initialized"));

    _context->bit_rate = bitrate;
}

r_codec_state r_audio_encoder::encode()
{
    raii_ptr<AVFrame> frame(av_frame_alloc(), [](AVFrame* f) { av_frame_free(&f); });

    frame.get()->format = _context->sample_fmt;
    frame.get()->sample_rate = _context->sample_rate;
    av_channel_layout_copy(&frame.get()->ch_layout, &_context->ch_layout);

    // Calculate number of samples based on buffer size and format
    int bytes_per_sample = av_get_bytes_per_sample(_context->sample_fmt);
    int channels = _context->ch_layout.nb_channels;

    // If frame_size is set, use it; otherwise calculate from buffer
    if(_context->frame_size > 0)
        frame.get()->nb_samples = _context->frame_size;
    else
        frame.get()->nb_samples = (int)(_buffer.size() / (bytes_per_sample * channels));

    auto res = av_frame_get_buffer(frame.get(), 0);
    if(res < 0)
        R_THROW(("Failed to allocate frame buffer: %s", _ff_rc_to_msg(res).c_str()));

    // Copy audio data into frame
    // Assume input buffer is always in the same format as the encoder expects
    size_t data_size = frame.get()->nb_samples * bytes_per_sample * channels;
    if(data_size > _buffer.size())
        data_size = _buffer.size();

    // Simply copy the data - the caller must provide data in the correct format
    if(av_sample_fmt_is_planar(_context->sample_fmt))
    {
        // Planar format: each channel's data is stored separately
        int samples_per_channel = frame.get()->nb_samples;
        int bytes_per_channel = samples_per_channel * bytes_per_sample;

        for(int ch = 0; ch < channels; ch++)
        {
            size_t offset = ch * bytes_per_channel;
            if(offset + bytes_per_channel <= _buffer.size())
            {
                memcpy(frame.get()->data[ch], _buffer.data() + offset, bytes_per_channel);
            }
        }
    }
    else
    {
        // Packed format: samples are interleaved
        memcpy(frame.get()->data[0], _buffer.data(), data_size);
    }

    frame.get()->pts = _pts;

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

r_codec_state r_audio_encoder::flush()
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
