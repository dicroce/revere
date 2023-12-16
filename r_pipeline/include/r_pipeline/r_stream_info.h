
#ifndef r_pipeline_r_stream_info_h
#define r_pipeline_r_stream_info_h

#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <string>
#include <map>
#include <vector>
#include <tuple>

namespace r_pipeline
{

enum r_media
{
    VIDEO_MEDIA,
    AUDIO_MEDIA
};

enum r_encoding
{
    H264_ENCODING,
    H265_ENCODING,
    AAC_LATM_ENCODING,
    AAC_GENERIC_ENCODING,
    PCMU_ENCODING,
    PCMA_ENCODING,
    AAL2_G726_16_ENCODING,
    AAL2_G726_24_ENCODING,
    AAL2_G726_32_ENCODING,
    AAL2_G726_40_ENCODING,
    G726_16_ENCODING,
    G726_24_ENCODING,
    G726_32_ENCODING,
    G726_40_ENCODING
};

R_API r_encoding str_to_encoding(const std::string& encoding_str);
R_API std::string encoding_to_str(r_encoding encoding);

R_API int encoding_to_pt(r_encoding encoding);

R_API std::string encoding_to_mime(r_encoding encoding);

struct r_h264_info
{
    std::string profile_level_id;
    std::string sprop_sps;
    std::string sprop_pps;
    int packetization_mode;
};

struct r_h265_info
{
    std::string sprop_vps;
    std::string sprop_sps;
    std::string sprop_pps;
};

struct r_aac_info
{
    r_utils::r_nullable<int> clock_rate;
};

struct r_pcmu_info
{
    r_utils::r_nullable<int> clock_rate;
};

struct r_pcma_info
{
    r_utils::r_nullable<int> clock_rate;
};

struct r_g726_info
{
    r_utils::r_nullable<int> clock_rate;
};

struct r_pad_info
{
    r_media media;
    int payload;
    int clock_rate;

    r_encoding encoding;

    r_utils::r_nullable<r_h264_info> h264;
    r_utils::r_nullable<r_h265_info> h265;
    r_utils::r_nullable<r_aac_info> aac;
    r_utils::r_nullable<r_pcmu_info> pcmu;
    r_utils::r_nullable<r_pcma_info> pcma;
    r_utils::r_nullable<r_g726_info> g726;

    r_utils::r_nullable<double> framerate;
};

struct r_rtp_map
{
    r_encoding encoding;
    int time_base;
};

struct r_sdp_media
{
    r_media type;
    std::vector<int> formats;
    std::map<int, r_rtp_map> rtpmaps;
    std::map<std::string, std::string> attributes;
};

R_API std::tuple<std::string, std::string, int> sdp_media_map_to_s(r_media m, const std::map<std::string, r_sdp_media>& sdp_media);
R_API std::tuple<std::string, std::string, int> sdp_media_to_s(r_sdp_media& sdp_media);

R_API r_utils::r_nullable<std::vector<uint8_t>> get_h264_sps(const std::string& video_codec_parameters);
R_API r_utils::r_nullable<std::vector<uint8_t>> get_h264_pps(const std::string& video_codec_parameters);

R_API r_utils::r_nullable<std::vector<uint8_t>> get_h265_vps(const std::string& video_codec_parameters);
R_API r_utils::r_nullable<std::vector<uint8_t>> get_h265_sps(const std::string& video_codec_parameters);
R_API r_utils::r_nullable<std::vector<uint8_t>> get_h265_pps(const std::string& video_codec_parameters);

R_API std::vector<uint8_t> make_h264_extradata(const r_utils::r_nullable<std::vector<uint8_t>>& sps, const r_utils::r_nullable<std::vector<uint8_t>>& pps);
R_API std::vector<uint8_t> make_h265_extradata(const r_utils::r_nullable<std::vector<uint8_t>>& vps, const r_utils::r_nullable<std::vector<uint8_t>>& sps, const r_utils::r_nullable<std::vector<uint8_t>>& pps);

struct r_h264_sps
{
    uint8_t profile_idc;
    uint8_t level_idc;
    uint16_t width;
    uint16_t height;
};

struct r_h265_sps
{
    uint8_t profile_idc;
    uint8_t level_idc;
    uint16_t width;
    uint16_t height;
};

R_API struct r_h264_sps parse_h264_sps(const std::vector<uint8_t>& sps);
R_API struct r_h265_sps parse_h265_sps(const std::vector<uint8_t>& sps);

R_API std::vector<uint8_t> get_video_codec_extradata(const std::string& video_codec_name, const std::string& video_codec_parameters);

}

#endif
