
#include "r_av/r_audio_decoder.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_std_utils.h"
#include <cstring>

using namespace r_av;
using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace std;

static const uint8_t READ_PADDING = 32;

static string _ff_rc_to_msg(int rc)
{
    char msg_buffer[1024];
    if(av_strerror(rc, msg_buffer, 1024) < 0)
        R_THROW(("Unknown ff return code."));
    return string(msg_buffer);
}

bool r_av::operator<(const r_resampler_state& lhs, const r_resampler_state& rhs)
{
    if(lhs.input_format < rhs.input_format) return true;
    if(lhs.input_format > rhs.input_format) return false;
    if(lhs.input_sample_rate < rhs.input_sample_rate) return true;
    if(lhs.input_sample_rate > rhs.input_sample_rate) return false;
    if(lhs.input_channels < rhs.input_channels) return true;
    if(lhs.input_channels > rhs.input_channels) return false;
    if(lhs.output_format < rhs.output_format) return true;
    if(lhs.output_format > rhs.output_format) return false;
    if(lhs.output_sample_rate < rhs.output_sample_rate) return true;
    if(lhs.output_sample_rate > rhs.output_sample_rate) return false;
    if(lhs.output_channels < rhs.output_channels) return true;
    return false;
}

r_audio_decoder::r_audio_decoder() :
    _codec_id(AV_CODEC_ID_NONE),
    _codec(nullptr),
    _context(nullptr),
    _buffer(nullptr),
    _buffer_size(0),
    _frame(nullptr),
    _resamplers(),
    _codec_opened(false)
{
}

r_audio_decoder::r_audio_decoder(AVCodecID codec_id) :
    _codec_id(codec_id),
    _codec(avcodec_find_decoder(_codec_id)),
    _context(avcodec_alloc_context3(_codec)),
    _buffer(nullptr),
    _buffer_size(0),
    _frame(av_frame_alloc()),
    _resamplers(),
    _codec_opened(false)
{
    if(!_codec)
        R_THROW(("Failed to find audio codec"));
    if(!_context)
        R_THROW(("Failed to allocate audio codec context"));
    if(!_frame)
        R_THROW(("Failed to allocate audio frame"));

    _context->extradata = nullptr;
    _context->extradata_size = 0;
}

r_audio_decoder::r_audio_decoder(r_audio_decoder&& obj) :
    _codec_id(std::move(obj._codec_id)),
    _codec(std::move(obj._codec)),
    _context(std::move(obj._context)),
    _buffer(std::move(obj._buffer)),
    _buffer_size(std::move(obj._buffer_size)),
    _frame(std::move(obj._frame)),
    _resamplers(std::move(obj._resamplers)),
    _codec_opened(std::move(obj._codec_opened))
{
    obj._codec_id = AV_CODEC_ID_NONE;
    obj._codec = nullptr;
    obj._context = nullptr;
    obj._buffer = nullptr;
    obj._buffer_size = 0;
    obj._frame = nullptr;
}

r_audio_decoder::~r_audio_decoder()
{
    _clear();
}

r_audio_decoder& r_audio_decoder::operator=(r_audio_decoder&& obj)
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
        _buffer = std::move(obj._buffer);
        obj._buffer = nullptr;
        _buffer_size = std::move(obj._buffer_size);
        obj._buffer_size = 0;
        _frame = std::move(obj._frame);
        obj._frame = nullptr;
        _resamplers = std::move(obj._resamplers);
        _codec_opened = std::move(obj._codec_opened);
    }

    return *this;
}

void r_audio_decoder::set_extradata(const vector<uint8_t>& ed)
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

void r_audio_decoder::set_pcm_params(int sample_rate, int channels)
{
    if(!_context)
        R_THROW(("Context is not initialized"));
    _context->sample_rate = sample_rate;
    av_channel_layout_default(&_context->ch_layout, channels);
}

void r_audio_decoder::_open_codec()
{
    if(_codec_opened)
        return;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "strict", "experimental", 0);
    int ret = avcodec_open2(_context, _codec, &opts);
    av_dict_free(&opts);
    if(ret < 0)
        R_THROW(("Failed to open audio codec: %s", _ff_rc_to_msg(ret).c_str()));

    _codec_opened = true;
}

void r_audio_decoder::attach_buffer(const uint8_t* data, size_t size)
{
    _buffer = data;
    _buffer_size = size;
}

r_codec_state r_audio_decoder::decode()
{
    _open_codec();

    auto packet = shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* pkt) { av_packet_free(&pkt); });
    packet->data = const_cast<uint8_t*>(_buffer);
    packet->size = (int)_buffer_size;

    int send_result = avcodec_send_packet(_context, packet.get());

    if(send_result >= 0)
    {
        int recv_result = avcodec_receive_frame(_context, _frame);

        if(recv_result >= 0)
            return R_CODEC_STATE_HAS_OUTPUT;
        else if(recv_result == AVERROR_EOF)
            return R_CODEC_STATE_EOF;
        else if(recv_result == AVERROR(EAGAIN))
            return R_CODEC_STATE_HUNGRY;
        else
        {
            R_LOG_ERROR("Failed to receive audio frame: %s", _ff_rc_to_msg(recv_result).c_str());
            return R_CODEC_STATE_HUNGRY;
        }
    }
    else if(send_result == AVERROR(EAGAIN))
    {
        int recv_result = avcodec_receive_frame(_context, _frame);

        if(recv_result >= 0)
            return R_CODEC_STATE_AGAIN_HAS_OUTPUT;
        else
            return R_CODEC_STATE_HUNGRY;
    }
    else if(send_result == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else
    {
        R_LOG_ERROR("Failed to send audio packet: %s", _ff_rc_to_msg(send_result).c_str());
        return R_CODEC_STATE_HUNGRY;
    }
}

r_codec_state r_audio_decoder::flush()
{
    _open_codec();

    int ret = avcodec_send_packet(_context, nullptr);
    if(ret == AVERROR(EAGAIN))
    {
        auto rf_ret = avcodec_receive_frame(_context, _frame);

        if(rf_ret == AVERROR(EAGAIN) || rf_ret == AVERROR_EOF)
            return R_CODEC_STATE_EOF;
        else if(rf_ret < 0)
            R_THROW(("Failed to flush audio decoder: %s", _ff_rc_to_msg(rf_ret).c_str()));

        return R_CODEC_STATE_HAS_OUTPUT;
    }
    else if(ret == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else if(ret < 0)
        R_THROW(("Failed to flush audio decoder: %s", _ff_rc_to_msg(ret).c_str()));

    auto rf_ret = avcodec_receive_frame(_context, _frame);
    if(rf_ret == AVERROR(EAGAIN) || rf_ret == AVERROR_EOF)
        return R_CODEC_STATE_EOF;
    else if(rf_ret < 0)
        R_THROW(("Failed to flush audio decoder: %s", _ff_rc_to_msg(rf_ret).c_str()));

    return R_CODEC_STATE_HAS_OUTPUT;
}

shared_ptr<vector<uint8_t>> r_audio_decoder::get(AVSampleFormat output_format, int output_sample_rate, int output_channels)
{
    int in_channels = _context->ch_layout.nb_channels;

    r_resampler_state state;
    state.input_format = _context->sample_fmt;
    state.input_sample_rate = _context->sample_rate;
    state.input_channels = in_channels;
    state.output_format = output_format;
    state.output_sample_rate = output_sample_rate;
    state.output_channels = output_channels;

    auto found = _resamplers.find(state);
    if(found == end(_resamplers))
    {
        SwrContext* swr = nullptr;
        AVChannelLayout in_layout = {}, out_layout = {};
        av_channel_layout_default(&in_layout, state.input_channels);
        av_channel_layout_default(&out_layout, state.output_channels);

        int ret = swr_alloc_set_opts2(
            &swr,
            &out_layout,
            state.output_format,
            state.output_sample_rate,
            &in_layout,
            state.input_format,
            state.input_sample_rate,
            0,
            nullptr
        );
        av_channel_layout_uninit(&in_layout);
        av_channel_layout_uninit(&out_layout);

        if(ret < 0 || !swr)
            R_THROW(("Failed to allocate audio resampler"));

        ret = swr_init(swr);
        if(ret < 0)
            R_THROW(("Failed to initialize audio resampler: %s", _ff_rc_to_msg(ret).c_str()));

        _resamplers[state] = swr;
    }

    auto swr = _resamplers[state];

    int out_samples = (int)av_rescale_rnd(
        swr_get_delay(swr, state.input_sample_rate) + _frame->nb_samples,
        output_sample_rate,
        state.input_sample_rate,
        AV_ROUND_UP
    );

    uint8_t** output_data = nullptr;
    int output_linesize = 0;
    int ret = av_samples_alloc_array_and_samples(
        &output_data,
        &output_linesize,
        output_channels,
        out_samples,
        output_format,
        0
    );
    if(ret < 0)
        R_THROW(("Failed to allocate audio output buffer: %s", _ff_rc_to_msg(ret).c_str()));

    int converted = swr_convert(
        swr,
        output_data,
        out_samples,
        (const uint8_t**)_frame->data,
        _frame->nb_samples
    );
    if(converted < 0)
    {
        av_freep(&output_data[0]);
        av_freep(&output_data);
        R_THROW(("Failed to resample audio: %s", _ff_rc_to_msg(converted).c_str()));
    }

    bool is_planar = av_sample_fmt_is_planar(output_format);
    int bytes_per_sample = av_get_bytes_per_sample(output_format);
    int bytes_per_channel = converted * bytes_per_sample;
    int total_bytes = bytes_per_channel * output_channels;

    auto result = make_shared<vector<uint8_t>>(total_bytes);

    if(!is_planar)
    {
        // Packed: all data is contiguous in output_data[0]
        memcpy(result->data(), output_data[0], total_bytes);
    }
    else
    {
        // Planar: channels are in separate buffers; store them sequentially
        for(int ch = 0; ch < output_channels; ch++)
            memcpy(result->data() + ch * bytes_per_channel, output_data[ch], bytes_per_channel);
    }

    av_freep(&output_data[0]);
    av_freep(&output_data);

    return result;
}

int r_audio_decoder::input_sample_rate() const
{
    return _context->sample_rate;
}

int r_audio_decoder::input_channels() const
{
    return _context->ch_layout.nb_channels;
}

AVSampleFormat r_audio_decoder::input_sample_format() const
{
    return _context->sample_fmt;
}

void r_audio_decoder::_clear()
{
    for(auto s : _resamplers)
        swr_free(&s.second);
    _resamplers.clear();

    if(_frame)
    {
        av_frame_free(&_frame);
        _frame = nullptr;
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
}
