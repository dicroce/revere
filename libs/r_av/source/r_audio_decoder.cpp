
#include "r_av/r_audio_decoder.h"
#include "r_utils/r_exception.h"
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
    if((lhs.input_format < rhs.input_format) ||
       ((lhs.input_format == rhs.input_format) && lhs.input_sample_rate < rhs.input_sample_rate) ||
       ((lhs.input_format == rhs.input_format) && (lhs.input_sample_rate == rhs.input_sample_rate) && lhs.input_channels < rhs.input_channels) ||
       ((lhs.input_format == rhs.input_format) && (lhs.input_sample_rate == rhs.input_sample_rate) && (lhs.input_channels == rhs.input_channels) && lhs.output_format < rhs.output_format) ||
       ((lhs.input_format == rhs.input_format) && (lhs.input_sample_rate == rhs.input_sample_rate) && (lhs.input_channels == rhs.input_channels) && (lhs.output_format == rhs.output_format) && lhs.output_sample_rate < rhs.output_sample_rate) ||
       ((lhs.input_format == rhs.input_format) && (lhs.input_sample_rate == rhs.input_sample_rate) && (lhs.input_channels == rhs.input_channels) && (lhs.output_format == rhs.output_format) && (lhs.output_sample_rate == rhs.output_sample_rate) && lhs.output_channels < rhs.output_channels))
        return true;
    return false;
}

r_audio_decoder::r_audio_decoder() :
    _codec_id(AV_CODEC_ID_NONE),
    _codec(nullptr),
    _context(nullptr),
    _parser(nullptr),
    _parse_input(false),
    _buffer(nullptr),
    _buffer_size(0),
    _pos(nullptr),
    _remaining_size(0),
    _frame(nullptr),
    _resamplers(),
    _codec_opened(false)
{
}

r_audio_decoder::r_audio_decoder(AVCodecID codec_id, bool parse_input) :
    _codec_id(codec_id),
    _codec(avcodec_find_decoder(_codec_id)),
    _context(avcodec_alloc_context3(_codec)),
    _parser(av_parser_init(_codec_id)),
    _parse_input(parse_input),
    _buffer(nullptr),
    _buffer_size(0),
    _pos(nullptr),
    _remaining_size(0),
    _frame(av_frame_alloc()),
    _resamplers(),
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

    if(_codec->capabilities & AV_CODEC_CAP_FRAME_THREADS)
        _context->thread_type = FF_THREAD_FRAME;
    else if(_codec->capabilities & AV_CODEC_CAP_SLICE_THREADS)
        _context->thread_type = FF_THREAD_SLICE;
    else _context->thread_count = 1; //don't use multithreading

    _context->flags |= AV_CODEC_FLAG_LOW_DELAY;  // Try reducing buffering
    _context->workaround_bugs = FF_BUG_AUTODETECT;  // Enable autodetection of bugs
}

r_audio_decoder::r_audio_decoder(r_audio_decoder&& obj) :
    _codec_id(std::move(obj._codec_id)),
    _codec(std::move(obj._codec)),
    _context(std::move(obj._context)),
    _parser(std::move(obj._parser)),
    _parse_input(std::move(obj._parse_input)),
    _buffer(std::move(obj._buffer)),
    _buffer_size(std::move(obj._buffer_size)),
    _pos(std::move(obj._pos)),
    _remaining_size(std::move(obj._remaining_size)),
    _frame(std::move(obj._frame)),
    _resamplers(std::move(obj._resamplers)),
    _codec_opened(std::move(obj._codec_opened))
{
    obj._codec_id = AV_CODEC_ID_NONE;
    obj._codec = nullptr;
    obj._context = nullptr;
    obj._parser = nullptr;
    obj._buffer = nullptr;
    obj._buffer_size = 0;
    obj._pos = nullptr;
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

void r_audio_decoder::_open_codec()
{
    if(_codec_opened)
        return;

    // Open the codec with explicit parameters
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "strict", "experimental", 0);  // Be more lenient with decoding
    int ret = avcodec_open2(_context, _codec, &opts);
    av_dict_free(&opts);
    if(ret < 0)
        R_THROW(("Failed to open codec: %s", _ff_rc_to_msg(ret).c_str()));

    _codec_opened = true;
}

void r_audio_decoder::attach_buffer(const uint8_t* data, size_t size)
{
    _buffer = data;
    _buffer_size = size;
    _pos = _buffer;
    _remaining_size = (int)_buffer_size;
}

r_codec_state r_audio_decoder::decode()
{
    _open_codec();

    // Create packet directly from your input buffer
    auto packet = shared_ptr<AVPacket>(av_packet_alloc(), [](AVPacket* pkt) { av_packet_free(&pkt); });
    // Point directly to your input buffer
    packet->data = const_cast<uint8_t*>(_pos);
    packet->size = _remaining_size;

    // Send the packet directly to the decoder
    int send_result = avcodec_send_packet(_context, packet.get());

    if (send_result >= 0) {
        // Entire packet was consumed
        _pos += _remaining_size;
        _remaining_size = 0;

        // Try to receive a frame
        int recv_result = avcodec_receive_frame(_context, _frame);

        if (recv_result >= 0)
            return R_CODEC_STATE_HAS_OUTPUT;
        else if (recv_result == AVERROR_EOF)
            return R_CODEC_STATE_EOF;
        else if (recv_result == AVERROR(EAGAIN))
            return R_CODEC_STATE_HUNGRY;
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

shared_ptr<vector<uint8_t>> r_audio_decoder::get(AVSampleFormat output_format, int output_sample_rate, int output_channels)
{
    r_resampler_state state;
    state.input_format = (AVSampleFormat)_frame->format;
    state.input_sample_rate = _frame->sample_rate;
    state.input_channels = _frame->ch_layout.nb_channels;
    state.output_format = output_format;
    state.output_sample_rate = output_sample_rate;
    state.output_channels = output_channels;

    auto found = _resamplers.find(state);

    if(found == end(_resamplers))
    {
        AVChannelLayout input_ch_layout;
        av_channel_layout_default(&input_ch_layout, state.input_channels);

        AVChannelLayout output_ch_layout;
        av_channel_layout_default(&output_ch_layout, state.output_channels);

        SwrContext* swr = nullptr;
        int ret = swr_alloc_set_opts2(
            &swr,
            &output_ch_layout,
            state.output_format,
            state.output_sample_rate,
            &input_ch_layout,
            state.input_format,
            state.input_sample_rate,
            0,
            nullptr
        );

        if(ret < 0 || !swr)
        {
            if(swr)
                swr_free(&swr);
            av_channel_layout_uninit(&input_ch_layout);
            av_channel_layout_uninit(&output_ch_layout);
            R_THROW(("Failed to allocate resampler: %s", _ff_rc_to_msg(ret).c_str()));
        }

        ret = swr_init(swr);
        if(ret < 0)
        {
            swr_free(&swr);
            av_channel_layout_uninit(&input_ch_layout);
            av_channel_layout_uninit(&output_ch_layout);
            R_THROW(("Failed to initialize resampler: %s", _ff_rc_to_msg(ret).c_str()));
        }

        _resamplers[state] = swr;

        av_channel_layout_uninit(&input_ch_layout);
        av_channel_layout_uninit(&output_ch_layout);
    }

    // Calculate output buffer size
    int output_samples = (int)av_rescale_rnd(
        swr_get_delay(_resamplers[state], state.input_sample_rate) + _frame->nb_samples,
        state.output_sample_rate,
        state.input_sample_rate,
        AV_ROUND_UP
    );

    int output_buffer_size = av_samples_get_buffer_size(
        nullptr,
        state.output_channels,
        output_samples,
        state.output_format,
        1
    );

    if(output_buffer_size < 0)
        R_THROW(("Failed to calculate output buffer size: %s", _ff_rc_to_msg(output_buffer_size).c_str()));

    auto result = make_shared<vector<uint8_t>>(output_buffer_size);

    // Prepare output buffer pointers for planar/packed formats
    uint8_t* output_ptrs[AV_NUM_DATA_POINTERS] = {nullptr};

    if(av_sample_fmt_is_planar(state.output_format))
    {
        // For planar output, set up pointers for each channel
        int bytes_per_sample = av_get_bytes_per_sample(state.output_format);
        int bytes_per_channel = output_samples * bytes_per_sample;
        for(int ch = 0; ch < state.output_channels; ch++)
        {
            output_ptrs[ch] = result->data() + ch * bytes_per_channel;
        }
    }
    else
    {
        // For packed output, single pointer
        output_ptrs[0] = result->data();
    }

    int converted_samples = swr_convert(
        _resamplers[state],
        output_ptrs,
        output_samples,
        (const uint8_t**)_frame->data,
        _frame->nb_samples
    );

    if(converted_samples < 0)
        R_THROW(("Failed to resample audio: %s", _ff_rc_to_msg(converted_samples).c_str()));

    // Resize to actual size
    int actual_size = av_samples_get_buffer_size(
        nullptr,
        state.output_channels,
        converted_samples,
        state.output_format,
        1
    );

    if(actual_size > 0)
        result->resize(actual_size);

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

AVSampleFormat r_audio_decoder::input_format() const
{
    return _context->sample_fmt;
}

void r_audio_decoder::_clear()
{
    for(auto r : _resamplers)
        swr_free(&r.second);
    _resamplers.clear();

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
}
