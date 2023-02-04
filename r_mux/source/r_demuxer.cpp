
#include "r_mux/r_demuxer.h"
#include "r_mux/r_format_utils.h"
#include "r_utils/r_string_utils.h"
#include <stdexcept>

using namespace std;
using namespace r_mux;
using namespace r_utils;

r_demuxer::r_demuxer(const std::string& fileName, bool annexBFilter) :
    _context([](AVFormatContext* fc){avformat_close_input(&fc);}),
    _video_stream_index(DEMUXER_STREAM_TYPE_UNKNOWN),
    _audio_stream_index(DEMUXER_STREAM_TYPE_UNKNOWN),
    _demux_pkt(),
    _filter_pkt(),
    _bsf([](AVBSFContext* bsf){av_bsf_free(&bsf);})
{
    _demux_pkt.size = 0;
    _demux_pkt.data = nullptr;

    int rc = avformat_open_input(&_context.raw(), fileName.c_str(), nullptr, nullptr);
    if(rc < 0)
        throw runtime_error(r_string_utils::format("Unable to avformat_open_input(): %s", ff_rc_to_msg(rc).c_str()));

    if(avformat_find_stream_info(_context.get(), nullptr) < 0)
        throw runtime_error("Unable to avformat_find_r_stream_info().");

    for(int i = 0; i < (int)_context.get()->nb_streams; ++i)
    {
        if(_context.get()->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if(_video_stream_index == DEMUXER_STREAM_TYPE_UNKNOWN)
                _video_stream_index = i;
        }
        else if(_context.get()->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            if(_audio_stream_index == DEMUXER_STREAM_TYPE_UNKNOWN)
                _audio_stream_index = i;
        }
    }

    if(_video_stream_index == DEMUXER_STREAM_TYPE_UNKNOWN && _audio_stream_index == DEMUXER_STREAM_TYPE_UNKNOWN)
        throw runtime_error("Unable to locate video or audio stream!");

    if(annexBFilter && _context.get()->streams[_video_stream_index]->codecpar->codec_id == AV_CODEC_ID_H264)
        _init_annexb_filter();

    _context.get()->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
}

r_demuxer::r_demuxer(r_demuxer&& obj) :
    _context(std::move(obj._context)),
    _video_stream_index(std::move(obj._video_stream_index)),
    _audio_stream_index(std::move(obj._audio_stream_index)),
    _demux_pkt(std::move(obj._demux_pkt)),
    _filter_pkt(std::move(obj._filter_pkt)),
    _bsf(std::move(obj._bsf))
{
    obj._context = NULL;
    obj._bsf = NULL;
}

r_demuxer::~r_demuxer()
{
    _free_demux_pkt();
    _free_filter_pkt();
}

r_demuxer& r_demuxer::operator=(r_demuxer&& obj)
{
    _context = std::move(obj._context);
    obj._context = NULL;
    _video_stream_index = std::move(obj._video_stream_index);
    _audio_stream_index = std::move(obj._audio_stream_index);
    _demux_pkt = std::move(obj._demux_pkt);
    _filter_pkt = std::move(obj._filter_pkt);
    _bsf = std::move(obj._bsf);
    obj._bsf = NULL;

    return *this;
}

int r_demuxer::get_stream_count() const
{
    return _context.get()->nb_streams;
}

int r_demuxer::get_video_stream_index() const
{
    if(_video_stream_index == DEMUXER_STREAM_TYPE_UNKNOWN)
        throw runtime_error("Unable to locate video stream!");

    return _video_stream_index;
}

int r_demuxer::get_audio_stream_index() const
{
    if(_audio_stream_index == DEMUXER_STREAM_TYPE_UNKNOWN)
        throw runtime_error("Unable to locate audio stream!");

    return _audio_stream_index;
}

struct r_stream_info r_demuxer::get_stream_info(int stream_index) const
{
    struct r_stream_info si;
    auto stream = _context.get()->streams[stream_index];
    si.codec_id = stream->codecpar->codec_id;
    si.time_base.num = stream->time_base.num;
    si.time_base.den = stream->time_base.den;
    si.extradata = get_extradata(stream_index);

    if(stream_index == _video_stream_index)
    {
        auto fr = av_guess_frame_rate(_context.get(), stream, NULL);
        si.frame_rate.num = fr.num;
        si.frame_rate.den = fr.den;
        si.resolution = make_pair((uint16_t)stream->codecpar->width, (uint16_t)stream->codecpar->height);
        si.profile = stream->codecpar->profile;
        si.level = stream->codecpar->level;
    }
    else
    {
        si.bits_per_raw_sample = stream->codecpar->bits_per_raw_sample;
        si.channels = stream->codecpar->channels;
        si.sample_rate = stream->codecpar->sample_rate;
    }

    return si;
}

vector<uint8_t> r_demuxer::get_extradata(int stream_index) const
{
    auto s = _context.get()->streams[stream_index];

    vector<uint8_t> ed;

    if(s->codecpar->extradata_size > 0)
    {
        ed.resize(s->codecpar->extradata_size);
        memcpy(&ed[0], s->codecpar->extradata, s->codecpar->extradata_size);
    }

    return ed;
}

bool r_demuxer::read_frame()
{
    _free_demux_pkt();

    if(av_read_frame(_context.get(), &_demux_pkt) >= 0)
    {
        // do optional annexb filter here if its video
        if(_demux_pkt.stream_index == _video_stream_index)
            _optional_annexb_filter();

        return true;
    }

    return false;
}

struct r_frame_info r_demuxer::get_frame_info() const
{
    const AVPacket* src_pkt = (_bsf && _demux_pkt.stream_index == _video_stream_index) ? &_filter_pkt : &_demux_pkt;

    r_frame_info fi;
    fi.index = (int)_demux_pkt.stream_index;
    fi.data = src_pkt->data;
    fi.size = src_pkt->size;
    fi.pts = src_pkt->pts;
    fi.dts = src_pkt->dts;
    fi.key = (src_pkt->flags & AV_PKT_FLAG_KEY) ? true : false;

    return fi;
}

void r_demuxer::_free_demux_pkt()
{
    if(_demux_pkt.size > 0)
        av_packet_unref(&_demux_pkt);
}

void r_demuxer::_free_filter_pkt()
{
    if(_filter_pkt.size > 0)
        av_packet_unref(&_filter_pkt);
}

void r_demuxer::_init_annexb_filter()
{
    const AVBitStreamFilter* filter = av_bsf_get_by_name("h264_mp4toannexb");

    auto rc = av_bsf_alloc(filter, &_bsf.raw());
    if(rc < 0)
        throw runtime_error("Unable to av_bsf_alloc()");

    auto st = _context.get()->streams[get_video_stream_index()];

    rc = avcodec_parameters_copy(_bsf.get()->par_in, st->codecpar);
    if (rc < 0)
        throw runtime_error("Unable to avcodec_parameters_copy()");

    rc = av_bsf_init(_bsf.get());
    if(rc < 0)
        throw runtime_error("Unable to av_bsf_init()");

    rc = avcodec_parameters_copy(st->codecpar, _bsf.get()->par_out);
    if (rc < 0)
        throw runtime_error("Unable to avcodec_parameters_copy()");
}

void r_demuxer::_optional_annexb_filter()
{
    if(_bsf)
    {
        _free_filter_pkt();

        auto rc = av_bsf_send_packet(_bsf.get(), &_demux_pkt);
        if (rc < 0)
            throw runtime_error("Unable to av_bsf_send_packet()");

        rc = av_bsf_receive_packet(_bsf.get(), &_filter_pkt);
        if (rc < 0)
            throw runtime_error("Unable to av_bsf_receive_packet()");
    }
}
