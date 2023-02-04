
#include "r_pipeline/r_sample_context.h"

using namespace r_pipeline;
using namespace r_utils;
using namespace std;

r_nullable<double> sample_context::framerate() const
{
    r_nullable<double> value;
    if(_src_pad_info.find(VIDEO_MEDIA) != _src_pad_info.end())
    {
        if(!_src_pad_info.at(VIDEO_MEDIA).framerate.is_null())
            value.set_value(_src_pad_info.at(VIDEO_MEDIA).framerate.value());
    }
    return value;
}

r_nullable<uint8_t> sample_context::audio_channels() const
{
    return _audio_channels;
}

r_nullable<uint32_t> sample_context::audio_sample_rate() const
{
    return _audio_sample_rate;
}

int64_t sample_context::stream_start_ts() const
{
    return _stream_start_time;
}

string sample_context::sdp() const
{
    return _sdp_text;
}

r_sdp_media sample_context::sdp_media(r_media type) const
{
    return _sdp_medias.at((type==VIDEO_MEDIA)?"video":"audio");
}

uint64_t sample_context::gst_pts() const
{
    return _gst_time_info.pts;
}

uint64_t sample_context::gst_pts_running_time() const
{
    return _gst_time_info.pts_running_time;
}

uint64_t sample_context::gst_dts() const
{
    return _gst_time_info.dts;
}

uint64_t sample_context::gst_dts_running_time() const
{
    return _gst_time_info.dts_running_time;
}

r_nullable<string> sample_context::sprop_sps() const
{
    r_nullable<string> result;

    auto pi = _src_pad_info.find(VIDEO_MEDIA);

    if(pi != _src_pad_info.end())
    {
        auto rpi = pi->second;

        if(!rpi.h264.is_null())
            result.set_value(rpi.h264.value().sprop_sps);
        else if(!rpi.h265.is_null())
            result.set_value(rpi.h265.value().sprop_sps);
        else R_THROW(("No SPS!"));
    }

    return result;
}

r_nullable<string> sample_context::sprop_pps() const
{
    r_nullable<string> result;

    auto pi = _src_pad_info.find(VIDEO_MEDIA);

    if(pi != _src_pad_info.end())
    {
        auto rpi = pi->second;

        if(!rpi.h264.is_null())
            result.set_value(rpi.h264.value().sprop_pps);
        else if(!rpi.h265.is_null())
            result.set_value(rpi.h265.value().sprop_pps);
        else R_THROW(("No PPS!"));
    }

    return result;
}

r_nullable<string> sample_context::sprop_vps() const
{
    r_nullable<string> result;

    auto pi = _src_pad_info.find(VIDEO_MEDIA);

    if(pi != _src_pad_info.end())
    {
        auto rpi = pi->second;

        if(!rpi.h265.is_null())
            result.set_value(rpi.h265.value().sprop_vps);
        else R_THROW(("No VPS!"));
    }

    return result;
}
