
#ifndef r_mux_r_demuxer_h
#define r_mux_r_demuxer_h

#include "r_av/r_format_utils.h"
#include "r_utils/r_std_utils.h"
#include "r_utils/r_macro.h"

#include <string>
#include <vector>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

extern "C"
{
#include <libavcodec/bsf.h>
}

namespace r_av
{

enum r_demuxer_stream_type
{
    R_DEMUXER_STREAM_TYPE_UNKNOWN = -1,
    R_DEMUXER_STREAM_TYPE_VIDEO,
    R_DEMUXER_STREAM_TYPE_AUDIO,
};

struct r_stream_info
{
    AVCodecID codec_id;
    AVRational time_base;
    std::vector<uint8_t> extradata;
    int profile;
    int level;

    AVRational frame_rate;
    std::pair<uint16_t, uint16_t> resolution;

    uint8_t bits_per_raw_sample;
    uint8_t channels;
    uint32_t sample_rate;
};

struct r_frame_info
{
    int index;
    uint8_t* data;
    size_t size;
    int64_t pts;
    int64_t dts;
    bool key;
};

class r_demuxer final
{
public:
    R_API r_demuxer() {}
    R_API r_demuxer(const std::string& fileName, bool annexBFilter = true);
    R_API r_demuxer(const r_demuxer&) = delete;
    R_API r_demuxer(r_demuxer&& obj);
    R_API ~r_demuxer();

    R_API r_demuxer& operator=(const r_demuxer&) = delete;
    R_API r_demuxer& operator=(r_demuxer&& obj);

    R_API int get_stream_count() const;
    R_API int get_video_stream_index() const;
    R_API int get_audio_stream_index() const;

    R_API struct r_stream_info get_stream_info(int stream_index) const;

    R_API std::vector<uint8_t> get_extradata(int stream_index) const;

    R_API bool read_frame();
    R_API struct r_frame_info get_frame_info() const;

private:
    void _free_demux_pkt();
    void _free_filter_pkt();
    void _init_annexb_filter();
    void _optional_annexb_filter();

    r_utils::r_std_utils::raii_ptr<AVFormatContext> _context;
    int _video_stream_index;
    int _audio_stream_index;
    AVPacket _demux_pkt;
    AVPacket _filter_pkt;
    r_utils::r_std_utils::raii_ptr<AVBSFContext> _bsf;

};

}

#endif
