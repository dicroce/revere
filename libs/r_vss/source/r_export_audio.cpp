#include "r_vss/r_export_audio.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include <algorithm>
#include <cstring>

extern "C"
{
#include <libavformat/avformat.h>
}

using namespace r_vss;
using namespace r_utils;
using namespace std;

// A stored packet this much later than the samples before it ends them: the samples before are encoded (the last
// frame padded with silence) and the new ones start at the packet's own time, so audio after an outage stays in step
// with the video.
static const int64_t EXPORT_AUDIO_GAP_MS = 500;

int r_vss::export_container_holds(const string& file_name, AVCodecID codec_id)
{
    auto fmt = av_guess_format(nullptr, file_name.c_str(), nullptr);
    if(!fmt)
        return -1;
    auto r = avformat_query_codec(fmt, codec_id, FF_COMPLIANCE_NORMAL);
    return (r > 0) ? 1 : (r == 0) ? 0 : -1;
}

r_export_audio::r_export_audio(const string& codec_name, const string& codec_params, const string& file_name)
{
    try
    {
        _codec_id = r_av::encoding_to_av_codec_id(codec_name);
    }
    catch(...)
    {
        _mode = mode::drop;
        _why_dropped = "its codec (" + codec_name + ") isn't one the export knows";
        return;
    }

    r_nullable<int> rate, channels;
    for(auto& part : r_string_utils::split(codec_params, ","))
    {
        auto kv = r_string_utils::split(part, "=");
        if(kv.size() != 2)
            continue;
        try
        {
            if(r_string_utils::strip(kv[0]) == "sc_audio_rate")
                rate.set_value(r_string_utils::s_to_int(kv[1]));
            if(r_string_utils::strip(kv[0]) == "sc_audio_channels")
                channels.set_value(r_string_utils::s_to_int(kv[1]));
        }
        catch(...) {}
    }

    bool g711 = (_codec_id == AV_CODEC_ID_PCM_MULAW || _codec_id == AV_CODEC_ID_PCM_ALAW);
    if(channels.is_null() || channels.value() < 1)
        channels.set_value(1);
    if(rate.is_null() && g711)
        rate.set_value(8000);
    if(rate.is_null() || rate.value() <= 0)
    {
        _mode = mode::drop;
        _why_dropped = "its sample rate isn't stored";
        return;
    }
    _rate = rate.value();
    _channels = channels.value();

    if(export_container_holds(file_name, _codec_id) != 0)
    {
        _mode = mode::copy;
        return;
    }

    if(!g711 || export_container_holds(file_name, AV_CODEC_ID_AAC) == 0)
    {
        _mode = mode::drop;
        _why_dropped = "the file type can't hold " + codec_name + " audio";
        return;
    }

    try
    {
        _decoder = make_unique<r_av::r_audio_decoder>(_codec_id);
        _decoder->set_pcm_params(_rate, _channels);
        _encoder = _new_encoder();
        _pending.assign(_channels, {});
        _mode = mode::aac;
    }
    catch(const exception& ex)
    {
        _decoder.reset();
        _encoder.reset();
        _mode = mode::drop;
        _why_dropped = string("it couldn't be encoded as AAC: ") + ex.what();
    }
}

r_export_audio::~r_export_audio() noexcept = default;

unique_ptr<r_av::r_audio_encoder> r_export_audio::_new_encoder() const
{
    // AAC's top rate is about 6 bits a sample per channel; G.711 at 8 kHz gets 32 kb/s.
    auto bit_rate = (uint32_t)(_channels * (std::min)(64000, _rate * 4));
    return make_unique<r_av::r_audio_encoder>(AV_CODEC_ID_AAC, bit_rate, _rate, _channels, AV_SAMPLE_FMT_FLTP);
}

void r_export_audio::add_stream(r_av::r_muxer& muxer)
{
    if(_mode == mode::copy)
        muxer.add_audio_stream(_codec_id, (uint8_t)_channels, (uint32_t)_rate);
    else if(_mode == mode::aac)
    {
        muxer.add_audio_stream(AV_CODEC_ID_AAC, (uint8_t)_channels, (uint32_t)_rate);
        muxer.set_audio_extradata(_encoder->get_extradata());
    }
}

void r_export_audio::write(r_av::r_muxer& muxer, const uint8_t* data, size_t size, int64_t ts_ms)
{
    if(_mode == mode::copy)
    {
        muxer.write_audio_frame(const_cast<uint8_t*>(data), size, ts_ms, {1, 1000});
        return;
    }
    if(_mode != mode::aac || !data || !size)
        return;

    auto at_sample = ts_ms * _rate / 1000;
    if(!_anchored)
    {
        _next_sample = at_sample;
        _anchored = true;
    }
    else
    {
        auto pending_end = _next_sample + (int64_t)_pending[0].size();
        if((at_sample - pending_end) * 1000 / _rate > EXPORT_AUDIO_GAP_MS)
        {
            // The encoder holds a frame back (its delay): drain it here, and start a new one after the gap, or that
            // frame would be stamped with a time from the other side.
            flush(muxer);
            _encoder = _new_encoder();
            _next_sample = at_sample;
        }
    }

    _decoder->attach_buffer(data, size);
    auto state = _decoder->decode();
    if(state != r_av::R_CODEC_STATE_HAS_OUTPUT && state != r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT)
        return;

    // FLTP: each channel's samples one after the other.
    auto decoded = _decoder->get(AV_SAMPLE_FMT_FLTP, _rate, _channels);
    auto n = decoded->size() / (_channels * sizeof(float));
    auto src = reinterpret_cast<const float*>(decoded->data());
    for(int ch = 0; ch < _channels; ++ch)
        _pending[ch].insert(_pending[ch].end(), src + ch * n, src + (ch + 1) * n);

    _encode_pending(muxer, false);
}

void r_export_audio::flush(r_av::r_muxer& muxer)
{
    if(_mode != mode::aac)
        return;
    _encode_pending(muxer, true);
    while(_encoder->flush() == r_av::R_CODEC_STATE_HAS_OUTPUT)
        _write(muxer);
}

void r_export_audio::_encode_pending(r_av::r_muxer& muxer, bool pad)
{
    auto frame_size = (size_t)_encoder->get_frame_size();
    while(_pending[0].size() >= frame_size || (pad && !_pending[0].empty()))
    {
        if(_pending[0].size() < frame_size)
            for(auto& p : _pending)
                p.resize(frame_size, 0.0f);

        vector<uint8_t> frame(_channels * frame_size * sizeof(float));
        auto dst = reinterpret_cast<float*>(frame.data());
        for(int ch = 0; ch < _channels; ++ch)
        {
            memcpy(dst + ch * frame_size, _pending[ch].data(), frame_size * sizeof(float));
            _pending[ch].erase(_pending[ch].begin(), _pending[ch].begin() + frame_size);
        }

        _encoder->attach_buffer(frame.data(), frame.size(), _next_sample);
        _next_sample += (int64_t)frame_size;
        while(_encoder->encode() == r_av::R_CODEC_STATE_HAS_OUTPUT)
            _write(muxer);
    }
}

void r_export_audio::_write(r_av::r_muxer& muxer)
{
    auto pi = _encoder->get();
    muxer.write_audio_frame(pi.data, pi.size, pi.pts, pi.time_base);
}
