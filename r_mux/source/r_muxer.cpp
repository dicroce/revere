
#include "r_mux/r_muxer.h"
#include "r_mux/r_format_utils.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include <stdexcept>

using namespace std;
using namespace r_mux;
using namespace r_utils;
using namespace r_utils::r_std_utils;

AVCodecID r_mux::encoding_to_av_codec_id(const string& codec_name)
{
    auto lower_codec_name = r_string_utils::to_lower(codec_name);
    if(lower_codec_name == "h264")
        return AV_CODEC_ID_H264;
    else if(lower_codec_name == "h265" || lower_codec_name == "hevc")
        return AV_CODEC_ID_HEVC;
    else if(lower_codec_name == "mp4a-latm")
        return AV_CODEC_ID_AAC_LATM;
    else if(lower_codec_name == "mpeg4-generic")
        return AV_CODEC_ID_AAC;
    else if(lower_codec_name == "pcmu")
        return AV_CODEC_ID_PCM_MULAW;

    R_THROW(("Unknown codec name."));
}

r_muxer::r_muxer(const std::string& path, bool output_to_buffer) :
    _path(path),
    _output_to_buffer(output_to_buffer),
    _buffer(),
    _fc([](AVFormatContext* fc){avformat_free_context(fc);}),
    _video_stream(nullptr),
    _audio_stream(nullptr),
    _needs_finalize(false),
    _video_bsf([](AVBSFContext* bsf){av_bsf_free(&bsf);}),
    _audio_bsf([](AVBSFContext* bsf){av_bsf_free(&bsf);})

{
    avformat_alloc_output_context2(&_fc.raw(), NULL, NULL, _path.c_str());
    if(!_fc)
        R_THROW(("Unable to create libavformat output context"));

    if(_fc.get()->oformat->flags & AVFMT_GLOBALHEADER)
        _fc.get()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    _fc.get()->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
}

r_muxer::~r_muxer()
{
    if(_needs_finalize)
        finalize();
}

void r_muxer::add_video_stream(AVRational frame_rate, AVCodecID codec_id, uint16_t w, uint16_t h, int profile, int level)
{
    auto codec = avcodec_find_encoder(codec_id);
    if(!codec)
        R_THROW(("Unable to find encoder stream."));

    _video_stream = avformat_new_stream(_fc.get(), codec);
    if(!_video_stream)
        R_THROW(("Unable to allocate AVStream."));

    _video_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    _video_stream->codecpar->codec_id = codec_id;
    _video_stream->codecpar->width = w;
    _video_stream->codecpar->height = h;
    _video_stream->codecpar->format = AV_PIX_FMT_YUV420P;
    _video_stream->codecpar->profile = profile;
    _video_stream->codecpar->level = level;
    
    _video_stream->time_base.num = frame_rate.den;
    _video_stream->time_base.den = frame_rate.num;
}

void r_muxer::add_audio_stream(AVCodecID codec_id, uint8_t channels, uint32_t sample_rate)
{
    auto codec = avcodec_find_encoder(codec_id);

    _audio_stream = avformat_new_stream(_fc.get(), codec);
    if(!_audio_stream)
        R_THROW(("Unable to allocate AVStream"));

    _audio_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    _audio_stream->codecpar->codec_id = codec_id;
    _audio_stream->codecpar->channels = channels;
    _audio_stream->codecpar->sample_rate = sample_rate;
    _audio_stream->time_base.num = 1;
    _audio_stream->time_base.den = sample_rate;
}

void r_muxer::set_video_bitstream_filter(const std::string& filter_name)
{
    const AVBitStreamFilter *filter = av_bsf_get_by_name(filter_name.c_str());

    if(!filter)
        R_THROW(("Unable to find bitstream filter."));

    auto ret = av_bsf_alloc(filter, &_video_bsf.raw());
    if(ret < 0)
        R_STHROW(r_internal_exception, ("Unable to av_bsf_alloc()"));

    ret = avcodec_parameters_copy(_video_bsf.get()->par_in, _video_stream->codecpar);
    if (ret < 0)
        R_STHROW(r_internal_exception, ("Unable to avcodec_parameters_copy()"));

    ret = av_bsf_init(_video_bsf.get());
    if(ret < 0)
        R_STHROW(r_internal_exception, ("Unable to av_bsf_init()"));

    ret = avcodec_parameters_copy(_video_stream->codecpar, _video_bsf.get()->par_out);
    if (ret < 0)
        R_STHROW(r_internal_exception, ("Unable to avcodec_parameters_copy()"));
}

void r_muxer::set_audio_bitstream_filter(const std::string& filter_name)
{
    const AVBitStreamFilter *filter = av_bsf_get_by_name(filter_name.c_str());

    if(!filter)
        R_THROW(("Unable to find bitstream filter."));

    auto ret = av_bsf_alloc(filter, &_audio_bsf.raw());
    if(ret < 0)
        R_STHROW(r_internal_exception, ("Unable to av_bsf_alloc()"));

    ret = avcodec_parameters_copy(_audio_bsf.get()->par_in, _audio_stream->codecpar);
    if (ret < 0)
        R_STHROW(r_internal_exception, ("Unable to avcodec_parameters_copy()"));

    ret = av_bsf_init(_audio_bsf.get());
    if(ret < 0)
        R_STHROW(r_internal_exception, ("Unable to av_bsf_init()"));

    ret = avcodec_parameters_copy(_audio_stream->codecpar, _audio_bsf.get()->par_out);
    if (ret < 0)
        R_STHROW(r_internal_exception, ("Unable to avcodec_parameters_copy()"));
}

void r_muxer::set_video_extradata(const std::vector<uint8_t>& ed)
{
    if(!ed.empty())
    {
        if(_video_stream->codecpar->extradata)
            av_free(_video_stream->codecpar->extradata);

        _video_stream->codecpar->extradata = (uint8_t*)av_malloc(ed.size());
        memcpy(_video_stream->codecpar->extradata, &ed[0], ed.size());
        _video_stream->codecpar->extradata_size = (int)ed.size();
    }
}

void r_muxer::set_audio_extradata(const std::vector<uint8_t>& ed)
{
    if(!ed.empty())
    {
        if(_audio_stream->codecpar->extradata)
            av_free(_audio_stream->codecpar->extradata);

        _audio_stream->codecpar->extradata = (uint8_t*)av_malloc(ed.size());
        memcpy(_audio_stream->codecpar->extradata, &ed[0], ed.size());
        _audio_stream->codecpar->extradata_size = (int)ed.size();
    }
}

void r_muxer::open()
{
    if(_fc.get()->nb_streams < 1)
        R_THROW(("Please add a stream before opening this muxer."));

    if(_output_to_buffer)
    {
        int res = avio_open_dyn_buf(&_fc.get()->pb);
        if(res < 0)
            R_THROW(("Unable to allocate a memory IO object: %s", ff_rc_to_msg(res).c_str()));
    }
    else
    {
        int res = avio_open(&_fc.get()->pb, _path.c_str(), AVIO_FLAG_WRITE);
        if(res < 0)
            R_THROW(("Unable to open output io context: %s", ff_rc_to_msg(res).c_str()));
    }

    int res = avformat_write_header(_fc.get(), NULL);
    if(res < 0)
        R_THROW(("Unable to write header to output file: %s", ff_rc_to_msg(res).c_str()));

    _needs_finalize = true;
}

static void _get_packet_defaults(AVPacket* pkt)
{
    pkt->buf = nullptr;
    pkt->pts = AV_NOPTS_VALUE;
    pkt->dts = AV_NOPTS_VALUE;
    pkt->data = nullptr;
    pkt->size = 0;
    pkt->stream_index = 0;
    pkt->flags = 0;
    pkt->side_data = nullptr;
    pkt->side_data_elems = 0;
    pkt->duration = 0;
    pkt->pos = -1;
}

void r_muxer::write_video_frame(uint8_t* p, size_t size, int64_t input_pts, int64_t input_dts, AVRational input_time_base, bool key)
{
    if(_fc.get()->pb == nullptr)
        R_THROW(("Please call open() before writing frames."));

    raii_ptr<AVPacket> input_pkt(av_packet_alloc(), [](AVPacket* pkt){av_packet_free(&pkt);});
    _get_packet_defaults(input_pkt.get());

    input_pkt.get()->stream_index = _video_stream->index;
    input_pkt.get()->data = p;
    input_pkt.get()->size = (int)size;
    input_pkt.get()->pts = av_rescale_q(input_pts, input_time_base, _video_stream->time_base);
    input_pkt.get()->dts = av_rescale_q(input_dts, input_time_base, _video_stream->time_base);
    input_pkt.get()->flags |= (key)?AV_PKT_FLAG_KEY:0;

    if(_video_bsf)
    {
        int res = av_bsf_send_packet(_video_bsf.get(), input_pkt.get());

        if(res < 0)
            R_THROW(("Unable to send packet to bitstream filter: %s", ff_rc_to_msg(res).c_str()));

        while(res == 0)
        {
            raii_ptr<AVPacket> output_pkt(av_packet_alloc(), [](AVPacket* pkt){av_packet_free(&pkt);});
            _get_packet_defaults(output_pkt.get());

            res = av_bsf_receive_packet(_video_bsf.get(), output_pkt.get());

            if(res == 0)
            {
                res = av_interleaved_write_frame(_fc.get(), output_pkt.get());
                if(res < 0)
                    R_THROW(("Unable to write frame to output file: %s", ff_rc_to_msg(res).c_str()));
            }
            else if(res == AVERROR(EAGAIN))
            {
                // no output packet, needs more input
                break;
            }
            else R_THROW(("Unable to receive packet from bitstream filter: %s", ff_rc_to_msg(res).c_str()));
        }
    }
    else
    {
        int res = av_interleaved_write_frame(_fc.get(), input_pkt.get());
        if(res < 0)
            R_THROW(("Unable to write video frame to output file: %s", ff_rc_to_msg(res).c_str()));
    }
}

void r_muxer::write_audio_frame(uint8_t* p, size_t size, int64_t input_pts, AVRational input_time_base)
{
    if(_fc.get()->pb == nullptr)
        R_THROW(("Please call open() before writing frames."));

    if(_fc.get()->pb == nullptr)
        R_THROW(("Please call open() before writing frames."));

    raii_ptr<AVPacket> input_pkt(av_packet_alloc(), [](AVPacket* pkt){av_packet_free(&pkt);});
    _get_packet_defaults(input_pkt.get());

    input_pkt.get()->stream_index = _audio_stream->index;
    input_pkt.get()->data = p;
    input_pkt.get()->size = (int)size;
    input_pkt.get()->pts = av_rescale_q(input_pts, input_time_base, _audio_stream->time_base);
    input_pkt.get()->dts = input_pkt.get()->pts;
    input_pkt.get()->flags = AV_PKT_FLAG_KEY;

    if(_audio_bsf)
    {
        int res = av_bsf_send_packet(_audio_bsf.get(), input_pkt.get());

        if(res < 0)
            R_THROW(("Unable to send packet to bitstream filter: %s", ff_rc_to_msg(res).c_str()));

        while(res == 0)
        {
            raii_ptr<AVPacket> output_pkt(av_packet_alloc(), [](AVPacket* pkt){av_packet_free(&pkt);});
            _get_packet_defaults(output_pkt.get());

            res = av_bsf_receive_packet(_audio_bsf.get(), output_pkt.get());

            if(res == 0)
            {
                res = av_interleaved_write_frame(_fc.get(), output_pkt.get());
                if(res < 0)
                    R_THROW(("Unable to write frame to output file: %s", ff_rc_to_msg(res).c_str()));
            }
            else if(res == AVERROR(EAGAIN))
            {
                // no output packet, needs more input
                break;
            }
            else R_THROW(("Unable to receive packet from bitstream filter: %s", ff_rc_to_msg(res).c_str()));
        }
    }
    else
    {
        int res = av_interleaved_write_frame(_fc.get(), input_pkt.get());
        if(res < 0)
            R_THROW(("Unable to write audio frame to output file: %s", ff_rc_to_msg(res).c_str()));
    }
}

void r_muxer::finalize()
{
    if(_needs_finalize)
    {
        _needs_finalize = false;
        int res = av_write_trailer(_fc.get());
        if(res < 0)
            R_THROW(("Unable to write trailer to output file: %s", ff_rc_to_msg(res).c_str()));

        if(_output_to_buffer)
        {
            raii_ptr<uint8_t> fileBytes([](uint8_t* p){av_freep(p);});
            int fileSize = avio_close_dyn_buf(_fc.get()->pb, &fileBytes.raw());
            _buffer.resize(fileSize);
            memcpy(&_buffer[0], fileBytes.get(), fileSize);
        }
        else
        {
            int res = avio_close(_fc.get()->pb);
            if(res < 0)
                R_THROW(("Unable to close output io context: %s", ff_rc_to_msg(res).c_str()));
        }
    }
}

const uint8_t* r_muxer::buffer() const
{
    if(!_output_to_buffer)
        R_THROW(("Please only call buffer() on muxers configured to output to buffer."));

    return &_buffer[0];
}

size_t r_muxer::buffer_size() const
{
    if(!_output_to_buffer)
        R_THROW(("Please only call buffer_size() on muxers configured to output to buffer."));

    return _buffer.size();
}
