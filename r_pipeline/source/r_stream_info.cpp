#define GST_USE_UNSTABLE_API
#include <gst/codecparsers/gsth264parser.h>
#include <gst/codecparsers/gsth265parser.h>

#include "r_pipeline/r_stream_info.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_std_utils.h"
#include <cstring>

using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace r_pipeline;
using namespace std;

r_encoding r_pipeline::str_to_encoding(const std::string& encoding_str)
{
    auto lower_encoding = r_string_utils::to_lower(encoding_str);
    if(lower_encoding == "h264")
        return H264_ENCODING;
    else if(lower_encoding == "h265")
        return H265_ENCODING;
    else if(lower_encoding == "mp4a-latm")
        return AAC_LATM_ENCODING;
    else if(lower_encoding == "mpeg4-generic")
        return AAC_GENERIC_ENCODING;
    else if(lower_encoding == "pcmu")
        return PCMU_ENCODING;
    else if(lower_encoding == "pcma")
        return PCMA_ENCODING;
    else if(lower_encoding == "aal2-g726-16")
        return AAL2_G726_16_ENCODING;
    else if(lower_encoding == "aal2-g726-24")
        return AAL2_G726_24_ENCODING;
    else if(lower_encoding == "aal2-g726-32")
        return AAL2_G726_32_ENCODING;
    else if(lower_encoding == "aal2-g726-40")
        return AAL2_G726_40_ENCODING;
    else if(lower_encoding == "g726-16")
        return G726_16_ENCODING;
    else if(lower_encoding == "g726-24")
        return G726_24_ENCODING;
    else if(lower_encoding == "g726-32")
        return G726_32_ENCODING;
    else if(lower_encoding == "g726-40")
        return G726_40_ENCODING;

    R_THROW(("Unknown encoding: %s",encoding_str.c_str()));
}

string r_pipeline::encoding_to_str(r_encoding encoding)
{
    if(encoding == H264_ENCODING)
        return "h264";
    else if(encoding == H265_ENCODING)
        return "h265";
    else if(encoding == AAC_LATM_ENCODING)
        return "mp4a-latm";
    else if(encoding == AAC_GENERIC_ENCODING)
        return "mpeg4-generic";
    else if(encoding == PCMU_ENCODING)
        return "pcmu";
    else if(encoding == PCMA_ENCODING)
        return "pcma";
    else if(encoding == AAL2_G726_16_ENCODING)
        return "aal2-g726-16";
    else if(encoding == AAL2_G726_24_ENCODING)
        return "aal2-g726-24";
    else if(encoding == AAL2_G726_32_ENCODING)
        return "aal2-g726-32";
    else if(encoding == AAL2_G726_40_ENCODING)
        return "aal2-g726-40";
    else if(encoding == G726_16_ENCODING)
        return "g726-16";
    else if(encoding == G726_24_ENCODING)
        return "g726-24";
    else if(encoding == G726_32_ENCODING)
        return "g726-32";
    else if(encoding == G726_40_ENCODING)
        return "g726-40";

    R_THROW(("Unknown encoding: %d",encoding));
}

int r_pipeline::encoding_to_pt(r_encoding encoding)
{
    if(encoding == H264_ENCODING)
        return 96;
    else if(encoding == H265_ENCODING)
        return 96;
    else if(encoding == AAC_LATM_ENCODING)
        return 97;
    else if(encoding == AAC_GENERIC_ENCODING)
        return 97;
    else if(encoding == PCMU_ENCODING)
        return 0;
    else if(encoding == PCMA_ENCODING)
        return 8;
    else if(encoding == AAL2_G726_16_ENCODING)
        return 96;
    else if(encoding == AAL2_G726_24_ENCODING)
        return 96;
    else if(encoding == AAL2_G726_32_ENCODING)
        return 96;
    else if(encoding == AAL2_G726_40_ENCODING)
        return 96;
    else if(encoding == G726_16_ENCODING)
        return 96;
    else if(encoding == G726_24_ENCODING)
        return 96;
    else if(encoding == G726_32_ENCODING)
        return 96;
    else if(encoding == G726_40_ENCODING)
        return 96;

    R_THROW(("Unknown encoding: %d",encoding));
}

string r_pipeline::encoding_to_mime(r_encoding encoding)
{
    if(encoding == H264_ENCODING)
        return "video/x-h264";
    else if(encoding == H265_ENCODING)
        return "video/x-h265";
    else if(encoding == AAC_LATM_ENCODING)
        return "audio/mpeg";
    else if(encoding == AAC_GENERIC_ENCODING)
        return "audio/mpeg";
    else if(encoding == PCMU_ENCODING)
        return "audio/x-mulaw";
    else if(encoding == PCMA_ENCODING)
        return "audio/x-alaw";
    else if(encoding == AAL2_G726_16_ENCODING)
        return "audio/G726-16";
    else if(encoding == AAL2_G726_24_ENCODING)
        return "audio/G726-24";
    else if(encoding == AAL2_G726_32_ENCODING)
        return "audio/G726-32";
    else if(encoding == AAL2_G726_40_ENCODING)
        return "audio/G726-40";
    else if(encoding == G726_16_ENCODING)
        return "audio/G726-16";
    else if(encoding == G726_24_ENCODING)
        return "audio/G726-24";
    else if(encoding == G726_32_ENCODING)
        return "audio/G726-32";
    else if(encoding == G726_40_ENCODING)
        return "audio/G726-40";

    R_THROW(("Unknown encoding: %d",encoding));
}

tuple<string, string, int> r_pipeline::sdp_media_map_to_s(r_media m, const map<string, r_sdp_media>& sdp_media)
{
    auto sdp_m = sdp_media.at((m==VIDEO_MEDIA)?"video":"audio");
    if(sdp_m.formats.empty())
        R_THROW(("No formats in sdp media!"));
    return sdp_media_to_s(sdp_m);
}

tuple<string, string, int> r_pipeline::sdp_media_to_s(r_sdp_media& sdp_media)
{
    auto codec_name = encoding_to_str(sdp_media.rtpmaps[sdp_media.formats.front()].encoding);
    string codec_parameters;
    for(auto i = begin(sdp_media.attributes), e = end(sdp_media.attributes); i != e; ++i)
        codec_parameters += i->first + "=" + i->second + ((next(i) != e)?", ":"");
    return make_tuple(codec_name, codec_parameters, sdp_media.rtpmaps[sdp_media.formats.front()].time_base);
}

r_nullable<vector<uint8_t>> r_pipeline::get_h264_sps(const string& video_codec_parameters)
{
    //sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA
    auto params_parts = r_string_utils::split(video_codec_parameters, ", ");
    vector<uint8_t> start_code = {0, 0, 0, 1};

    vector<uint8_t> buffer;
    for(auto nvp : params_parts)
    {
        auto attribute = nvp.substr(0, nvp.find("="));

        if(attribute == "sprop-parameter-sets")
        {
            auto value = nvp.substr(nvp.find("=")+1);

            auto parts = r_string_utils::split(value, ",");

            auto sps = r_string_utils::from_base64(parts[0]);
            buffer.resize(4 + sps.size());
            memcpy(&buffer[0], &start_code[0], 4);
            memcpy(&buffer[4], &sps[0], sps.size());
        }
    }

    r_nullable<vector<uint8_t>> result;
    if(buffer.size() > 0)
        result = move(buffer);

    return result;
}

r_nullable<vector<uint8_t>> r_pipeline::get_h264_pps(const string& video_codec_parameters)
{
    //sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA
    auto params_parts = r_string_utils::split(video_codec_parameters, ", ");
    vector<uint8_t> start_code = {0, 0, 0, 1};

    vector<uint8_t> buffer;
    for(auto nvp : params_parts)
    {
        auto attribute = nvp.substr(0, nvp.find("="));

        if(attribute == "sprop-parameter-sets")
        {
            auto value = nvp.substr(nvp.find("=")+1);

            auto parts = r_string_utils::split(value, ",");

            auto pps = r_string_utils::from_base64(parts[1]);
            buffer.resize(4 + pps.size());
            memcpy(&buffer[0], &start_code[0], 4);
            memcpy(&buffer[4], &pps[0], pps.size());
        }
    }

    r_nullable<vector<uint8_t>> result;
    if(buffer.size() > 0)
        result = move(buffer);

    return result;
}

r_nullable<vector<uint8_t>> r_pipeline::get_h265_vps(const string& video_codec_parameters)
{
    auto params_parts = r_string_utils::split(video_codec_parameters, ", ");
    vector<uint8_t> start_code = {0, 0, 0, 1};

    vector<uint8_t> buffer;
    for(auto nvp : params_parts)
    {
        auto nv_parts = r_string_utils::split(nvp, "=");

        if(buffer.empty() && nv_parts[0] == "sprop-vps")
        {
            auto ps = r_string_utils::from_base64(nv_parts[1]);
            auto current_size = buffer.size();
            buffer.resize(4 + ps.size());
            memcpy(&buffer[0], &start_code[0], 4);
            memcpy(&buffer[4], &ps[0], ps.size());
        }
    }

    r_nullable<vector<uint8_t>> result;
    if(buffer.size() > 0)
        result = move(buffer);

    return result;
}

r_nullable<vector<uint8_t>> r_pipeline::get_h265_sps(const string& video_codec_parameters)
{
    auto params_parts = r_string_utils::split(video_codec_parameters, ", ");
    vector<uint8_t> start_code = {0, 0, 0, 1};

    vector<uint8_t> buffer;
    for(auto nvp : params_parts)
    {
        auto nv_parts = r_string_utils::split(nvp, "=");

        if(buffer.empty() && nv_parts[0] == "sprop-sps")
        {
            auto ps = r_string_utils::from_base64(nv_parts[1]);
            auto current_size = buffer.size();
            buffer.resize(4 + ps.size());
            memcpy(&buffer[0], &start_code[0], 4);
            memcpy(&buffer[4], &ps[0], ps.size());
        }
    }

    r_nullable<vector<uint8_t>> result;
    if(buffer.size() > 0)
        result = move(buffer);

    return result;
}

r_nullable<vector<uint8_t>> r_pipeline::get_h265_pps(const string& video_codec_parameters)
{    
    auto params_parts = r_string_utils::split(video_codec_parameters, ", ");
    vector<uint8_t> start_code = {0, 0, 0, 1};

    vector<uint8_t> buffer;
    for(auto nvp : params_parts)
    {
        auto nv_parts = r_string_utils::split(nvp, "=");

        if(buffer.empty() && nv_parts[0] == "sprop-pps")
        {
            auto ps = r_string_utils::from_base64(nv_parts[1]);
            auto current_size = buffer.size();
            buffer.resize(4 + ps.size());
            memcpy(&buffer[0], &start_code[0], 4);
            memcpy(&buffer[4], &ps[0], ps.size());
        }
    }

    r_nullable<vector<uint8_t>> result;
    if(buffer.size() > 0)
        result = move(buffer);

    return result;
}

vector<uint8_t> r_pipeline::make_h264_extradata(
    const r_nullable<vector<uint8_t>>& sps,
    const r_nullable<vector<uint8_t>>& pps
)
{
    std::vector<uint8_t> start_code = {0x00, 0x00, 0x00, 0x01};

    auto sps_b = (!sps.is_null())?sps.value():vector<uint8_t>();
    auto pps_b = (!pps.is_null())?pps.value():vector<uint8_t>();
    vector<uint8_t> output(sps_b.size() + pps_b.size() + (start_code.size() * 2));
    uint8_t* writer = &output[0];
    if(!sps_b.empty())
    {
        memcpy(writer, &start_code[0], start_code.size());
        writer += start_code.size();
        memcpy(writer, &sps_b[0], sps_b.size());
        writer += sps_b.size();
    }
    if(!pps_b.empty())
    {
        memcpy(writer, &start_code[0], start_code.size());
        writer += start_code.size();
        memcpy(&output[sps_b.size()], &pps_b[0], pps_b.size());
    }
    return output;
}

vector<uint8_t> r_pipeline::make_h265_extradata(
    const r_nullable<vector<uint8_t>>& vps,
    const r_nullable<vector<uint8_t>>& sps,
    const r_nullable<vector<uint8_t>>& pps
)
{
    auto vps_b = (!vps.is_null())?vps.value():vector<uint8_t>();
    auto sps_b = (!sps.is_null())?sps.value():vector<uint8_t>();
    auto pps_b = (!pps.is_null())?pps.value():vector<uint8_t>();
    vector<uint8_t> output(vps_b.size() + sps_b.size() + pps_b.size());
    if(!vps_b.empty())
        memcpy(&output[0], &vps_b[0], vps_b.size());
    if(!sps_b.empty())
        memcpy(&output[vps_b.size()], &sps_b[0], sps_b.size());
    if(!pps_b.empty())
        memcpy(&output[vps_b.size() + sps_b.size()], &pps_b[0], pps_b.size());
    return output;
}

struct r_h264_sps r_pipeline::parse_h264_sps(const vector<uint8_t>& sps)
{
    raii_ptr<GstH264NalParser> nal_parser(gst_h264_nal_parser_new(), [](GstH264NalParser* p){gst_h264_nal_parser_free(p);});

    GstH264NalUnit nal_unit;
    
    auto rc = gst_h264_parser_identify_nalu(nal_parser.get(), &sps[0], 0, sps.size(), &nal_unit);

    if(nal_unit.type != GST_H264_NAL_SPS || ((rc != GST_H264_PARSER_OK) && (rc != GST_H264_PARSER_NO_NAL_END)))
        R_THROW(("Unable to locate h264 sps."));

    GstH264SPS sps_info;
    gst_h264_parser_parse_sps(nal_parser.get(), &nal_unit, &sps_info);
    r_h264_sps result;
    result.profile_idc = sps_info.profile_idc;
    result.level_idc = sps_info.level_idc;
    result.width = (uint16_t)sps_info.width;
    result.height = (uint16_t)sps_info.height;

    return result;
}

struct r_h265_sps r_pipeline::parse_h265_sps(const vector<uint8_t>& sps)
{
    raii_ptr<GstH265Parser> nal_parser(gst_h265_parser_new(), [](GstH265Parser* p){gst_h265_parser_free(p);});

    GstH265NalUnit nal_unit;
    
    auto rc = gst_h265_parser_identify_nalu(nal_parser.get(), &sps[0], 0, sps.size(), &nal_unit);

    if(nal_unit.type != GST_H265_NAL_SPS || ((rc != GST_H265_PARSER_OK) && (rc != GST_H265_PARSER_NO_NAL_END)))
        R_THROW(("Unable to locate h265 sps."));

    GstH265SPS sps_info;
    gst_h265_parser_parse_sps(nal_parser.get(), &nal_unit, &sps_info, false);

    r_h265_sps result;
    result.profile_idc = sps_info.profile_tier_level.profile_idc;
    result.level_idc = sps_info.profile_tier_level.level_idc;
    result.width = (uint16_t)sps_info.width;
    result.height = (uint16_t)sps_info.height;

    return result;
}

vector<uint8_t> r_pipeline::get_video_codec_extradata(const std::string& video_codec_name, const std::string& video_codec_parameters)
{
    auto lower_codec_name = r_string_utils::to_lower(video_codec_name);

    if( lower_codec_name == "h264")
        return make_h264_extradata(get_h264_sps(video_codec_parameters), get_h264_pps(video_codec_parameters));
    else if(lower_codec_name == "h265")
        return make_h265_extradata(get_h265_vps(video_codec_parameters), get_h265_sps(video_codec_parameters), get_h265_pps(video_codec_parameters));
    else R_THROW(("Unknown video codec name"));
}
