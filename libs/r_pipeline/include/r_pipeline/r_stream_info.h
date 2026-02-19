
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
    PCMA_ENCODING
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

// Build a proper AVCDecoderConfigurationRecord (AVCC format) for use as MP4 muxer extradata.
// sps and pps are expected in the format returned by get_h264_sps/get_h264_pps:
// [00 00 00 01][NALU bytes]. The start-code prefix is stripped internally.
// Unlike make_h264_extradata (which produces Annex-B), this produces AVCC so that
// FFmpeg's MP4 muxer correctly converts incoming Annex-B frame data to length-prefixed
// AVCC inside the container.
R_API std::vector<uint8_t> make_h264_avcc_extradata(const r_utils::r_nullable<std::vector<uint8_t>>& sps, const r_utils::r_nullable<std::vector<uint8_t>>& pps);

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

// Inverse of get_video_codec_extradata: parse AVCC (H264) or HVCC (H265) binary extradata
// and produce the codec_parameters string used elsewhere in the system, e.g.:
//   H264: "sprop-parameter-sets=<sps_b64>,<pps_b64>,sc_framerate=<fr>"
//   H265: "sprop-vps=<b64>,sprop-sps=<b64>,sprop-pps=<b64>,sc_framerate=<fr>"
R_API std::string build_video_codec_params(const std::string& codec_name, const std::vector<uint8_t>& extradata, float framerate);

// Build an audio_codec_parameters string encoding rate, channels, and (optionally)
// the AudioSpecificConfig (ASC) as sc_asc=<base64>, e.g.:
//   "sc_audio_rate=48000, sc_audio_channels=2, sc_asc=<b64>"
R_API std::string build_audio_codec_params(const std::vector<uint8_t>& asc, int sample_rate, int channels);

// Extract the AudioSpecificConfig (ASC) from an audio_codec_parameters string.
// Returns an empty vector if sc_asc is absent.
R_API std::vector<uint8_t> get_audio_codec_extradata(const std::string& audio_codec_parameters);

}

#endif
