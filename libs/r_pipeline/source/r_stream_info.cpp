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

            // Bounds check: ensure parts[0] exists
            if(parts.size() < 1)
                continue;

            auto sps = r_string_utils::from_base64(parts[0]);
            if(sps.empty())
                continue;

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

            // Bounds check: ensure parts[1] exists (need at least 2 parts for PPS)
            if(parts.size() < 2)
                continue;

            auto pps = r_string_utils::from_base64(parts[1]);
            if(pps.empty())
                continue;

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

        // Bounds check: ensure we have key=value pair
        if(nv_parts.size() < 2)
            continue;

        if(buffer.empty() && nv_parts[0] == "sprop-vps")
        {
            auto ps = r_string_utils::from_base64(nv_parts[1]);
            if(ps.empty())
                continue;

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

        // Bounds check: ensure we have key=value pair
        if(nv_parts.size() < 2)
            continue;

        if(buffer.empty() && nv_parts[0] == "sprop-sps")
        {
            auto ps = r_string_utils::from_base64(nv_parts[1]);
            if(ps.empty())
                continue;

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

        // Bounds check: ensure we have key=value pair
        if(nv_parts.size() < 2)
            continue;

        if(buffer.empty() && nv_parts[0] == "sprop-pps")
        {
            auto ps = r_string_utils::from_base64(nv_parts[1]);
            if(ps.empty())
                continue;

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
        memcpy(writer, &pps_b[0], pps_b.size());
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

vector<uint8_t> r_pipeline::make_h264_avcc_extradata(
    const r_nullable<vector<uint8_t>>& sps,
    const r_nullable<vector<uint8_t>>& pps
)
{
    // sps.value() = [00 00 00 01][SPS NALU bytes]  (as returned by get_h264_sps)
    // pps.value() = [00 00 00 01][PPS NALU bytes]  (as returned by get_h264_pps)
    // Strip the 4-byte Annex-B start code to get the raw NALU bytes.
    if(sps.is_null() || sps.value().size() <= 4)
        R_THROW(("Cannot build AVCC extradata: SPS is missing or too short"));

    const auto& sps_full = sps.value();
    const uint8_t* raw_sps = sps_full.data() + 4;
    size_t raw_sps_size    = sps_full.size()  - 4;

    if(raw_sps_size < 4)
        R_THROW(("SPS NALU too short to extract profile/level for AVCC record"));

    // AVCDecoderConfigurationRecord (ISO 14496-15 §5.2.4.1.1)
    //   [0x01]             configurationVersion
    //   [sps[1]]           AVCProfileIndication   (profile_idc)
    //   [sps[2]]           profile_compatibility  (constraint_set_flags)
    //   [sps[3]]           AVCLevelIndication     (level_idc)
    //   [0xFF]             lengthSizeMinusOne=3 | 0xFC  → 4-byte NALU lengths
    //   [0xE1]             numSPS=1 | 0xE0
    //   [len_hi][len_lo]   SPS length (big-endian)
    //   [SPS bytes]
    //   [0x01]             numPPS
    //   [len_hi][len_lo]   PPS length (big-endian)
    //   [PPS bytes]
    vector<uint8_t> avcc;
    avcc.push_back(0x01);
    avcc.push_back(raw_sps[1]);
    avcc.push_back(raw_sps[2]);
    avcc.push_back(raw_sps[3]);
    avcc.push_back(0xFF);
    avcc.push_back(0xE1);
    avcc.push_back((raw_sps_size >> 8) & 0xFF);
    avcc.push_back(raw_sps_size & 0xFF);
    avcc.insert(avcc.end(), raw_sps, raw_sps + raw_sps_size);

    if(!pps.is_null() && pps.value().size() > 4)
    {
        const auto& pps_full = pps.value();
        const uint8_t* raw_pps = pps_full.data() + 4;
        size_t raw_pps_size    = pps_full.size()  - 4;

        avcc.push_back(0x01);
        avcc.push_back((raw_pps_size >> 8) & 0xFF);
        avcc.push_back(raw_pps_size & 0xFF);
        avcc.insert(avcc.end(), raw_pps, raw_pps + raw_pps_size);
    }
    else
    {
        avcc.push_back(0x00); // numPPS = 0
    }

    return avcc;
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

static bool _is_annexb(const vector<uint8_t>& ed)
{
    if(ed.size() >= 4 && ed[0] == 0 && ed[1] == 0 && ed[2] == 0 && ed[3] == 1) return true;
    if(ed.size() >= 3 && ed[0] == 0 && ed[1] == 0 && ed[2] == 1) return true;
    return false;
}

// Scan Annex-B data and return raw NALU bytes (without start code) keyed by NAL unit type.
// H.264: type = data[0] & 0x1F
// H.265: type = (data[0] >> 1) & 0x3F  (pass h265=true)
static map<uint8_t, vector<uint8_t>> _scan_annexb_nalus(const vector<uint8_t>& data, bool h265)
{
    map<uint8_t, vector<uint8_t>> out;
    vector<pair<size_t,size_t>> nals;
    size_t i = 0;
    while(i + 2 < data.size())
    {
        if(data[i] == 0 && data[i+1] == 0 && data[i+2] == 1)
        {
            size_t nal_start = i + 3;
            if(!nals.empty())
            {
                size_t nal_end = i;
                while(nal_end > nals.back().first && data[nal_end - 1] == 0)
                    --nal_end;
                nals.back().second = nal_end;
            }
            nals.push_back({nal_start, data.size()});
            i = nal_start;
        }
        else ++i;
    }
    for(auto& [ns, ne] : nals)
    {
        if(ns >= ne || ns >= data.size()) continue;
        uint8_t t = h265 ? ((data[ns] >> 1) & 0x3F) : (data[ns] & 0x1F);
        if(!out.count(t))
            out[t] = vector<uint8_t>(data.begin() + ns, data.begin() + ne);
    }
    return out;
}

// Build "sprop-parameter-sets=<sps_b64>,<pps_b64>,sc_framerate=<fr>" from H264 extradata.
// Handles both Annex-B (encoder output) and AVCC (AVCDecoderConfigurationRecord) formats.
static string _build_h264_codec_params(const vector<uint8_t>& extradata, float framerate)
{
    if(extradata.size() < 4)
        R_THROW(("H264 extradata too short."));

    vector<uint8_t> sps_bytes, pps_bytes;

    if(_is_annexb(extradata))
    {
        auto nalus = _scan_annexb_nalus(extradata, false);
        if(nalus.count(7)) sps_bytes = nalus[7];
        if(nalus.count(8)) pps_bytes = nalus[8];
    }
    else
    {
        // AVCC layout (AVCDecoderConfigurationRecord):
        //   [0]      version (1)
        //   [1..3]   profile, compat, level
        //   [4]      0xFF  (nalu_length_size - 1 = 3, stored as | 0xFC)
        //   [5]      0xE0 | num_sps
        //   [6..7]   sps_length  (big-endian)
        //   [8..]    sps bytes
        //   [8+sps]  num_pps
        //   [9+sps..10+sps] pps_length (big-endian)
        //   [11+sps..] pps bytes
        if(extradata.size() < 7)
            R_THROW(("AVCC extradata too short to parse SPS/PPS."));

        size_t idx = 5;
        int num_sps = extradata[idx] & 0x1F;
        idx++;

        for(int i = 0; i < num_sps && idx + 1 < extradata.size(); ++i)
        {
            uint16_t sps_len = ((uint16_t)extradata[idx] << 8) | extradata[idx + 1];
            idx += 2;
            if(idx + sps_len > extradata.size()) break;
            if(i == 0)
                sps_bytes.assign(extradata.begin() + idx, extradata.begin() + idx + sps_len);
            idx += sps_len;
        }

        if(idx < extradata.size())
        {
            int num_pps = extradata[idx];
            idx++;
            for(int i = 0; i < num_pps && idx + 1 < extradata.size(); ++i)
            {
                uint16_t pps_len = ((uint16_t)extradata[idx] << 8) | extradata[idx + 1];
                idx += 2;
                if(idx + pps_len > extradata.size()) break;
                if(i == 0)
                    pps_bytes.assign(extradata.begin() + idx, extradata.begin() + idx + pps_len);
                idx += pps_len;
            }
        }
    }

    auto sps_b64 = r_string_utils::to_base64(sps_bytes.data(), sps_bytes.size());
    auto pps_b64 = r_string_utils::to_base64(pps_bytes.data(), pps_bytes.size());

    return "sprop-parameter-sets=" + sps_b64 + "," + pps_b64 +
           ",sc_framerate=" + to_string(framerate);
}

// Build "sprop-vps=<b64>,sprop-sps=<b64>,sprop-pps=<b64>,sc_framerate=<fr>" from H265 extradata.
// Handles both Annex-B (encoder output) and HVCC (HEVCDecoderConfigurationRecord) formats.
static string _build_h265_codec_params(const vector<uint8_t>& extradata, float framerate)
{
    if(extradata.size() < 4)
        R_THROW(("H265 extradata too short."));

    vector<uint8_t> vps_bytes, sps_bytes, pps_bytes;

    if(_is_annexb(extradata))
    {
        // H.265 Annex-B: NAL unit type = (first_byte >> 1) & 0x3F
        // VPS=32, SPS=33, PPS=34
        auto nalus = _scan_annexb_nalus(extradata, true);
        if(nalus.count(32)) vps_bytes = nalus[32];
        if(nalus.count(33)) sps_bytes = nalus[33];
        if(nalus.count(34)) pps_bytes = nalus[34];
    }
    else
    {
        // HVCC format (HEVCDecoderConfigurationRecord, ISO 14496-15 §8.3.3):
        //   22 bytes of profile/level/misc header, then:
        //   [22]     numOfArrays
        //   For each array:
        //     [0]    array_completeness(1) | reserved(1) | NAL_unit_type(6)
        //     [1..2] numNalus  (big-endian)
        //     For each NALU:
        //       [0..1] nalUnitLength (big-endian)
        //       [2..]  NALU data
        //   NAL types: 32=VPS, 33=SPS, 34=PPS
        if(extradata.size() < 23)
            R_THROW(("HVCC extradata too short to parse VPS/SPS/PPS."));

        size_t idx = 22;
        int num_arrays = extradata[idx];
        idx++;

        for(int a = 0; a < num_arrays && idx < extradata.size(); ++a)
        {
            uint8_t nal_unit_type = extradata[idx] & 0x3F;
            idx++;

            if(idx + 1 >= extradata.size()) break;
            uint16_t num_nalus = ((uint16_t)extradata[idx] << 8) | extradata[idx + 1];
            idx += 2;

            for(int n = 0; n < num_nalus && idx + 1 < extradata.size(); ++n)
            {
                uint16_t nalu_len = ((uint16_t)extradata[idx] << 8) | extradata[idx + 1];
                idx += 2;
                if(idx + nalu_len > extradata.size()) break;

                if(n == 0)
                {
                    auto nalu = vector<uint8_t>(extradata.begin() + idx, extradata.begin() + idx + nalu_len);
                    if(nal_unit_type == 32)       vps_bytes = move(nalu);
                    else if(nal_unit_type == 33)  sps_bytes = move(nalu);
                    else if(nal_unit_type == 34)  pps_bytes = move(nalu);
                }
                idx += nalu_len;
            }
        }
    }

    auto vps_b64 = r_string_utils::to_base64(vps_bytes.data(), vps_bytes.size());
    auto sps_b64 = r_string_utils::to_base64(sps_bytes.data(), sps_bytes.size());
    auto pps_b64 = r_string_utils::to_base64(pps_bytes.data(), pps_bytes.size());

    return "sprop-vps=" + vps_b64 +
           ",sprop-sps=" + sps_b64 +
           ",sprop-pps=" + pps_b64 +
           ",sc_framerate=" + to_string(framerate);
}

string r_pipeline::build_video_codec_params(const string& codec_name, const vector<uint8_t>& extradata, float framerate)
{
    auto lower = r_string_utils::to_lower(codec_name);
    if(lower == "h264")
        return _build_h264_codec_params(extradata, framerate);
    else if(lower == "h265" || lower == "hevc")
        return _build_h265_codec_params(extradata, framerate);
    else
        R_THROW(("Unsupported codec for build_video_codec_params: %s", codec_name.c_str()));
}

string r_pipeline::build_audio_codec_params(const vector<uint8_t>& asc, int sample_rate, int channels)
{
    string result = "sc_audio_rate=" + to_string(sample_rate) +
                    ", sc_audio_channels=" + to_string(channels);
    if(!asc.empty())
        result += ", sc_asc=" + r_string_utils::to_base64(asc.data(), asc.size());
    return result;
}

vector<uint8_t> r_pipeline::get_audio_codec_extradata(const string& audio_codec_parameters)
{
    vector<uint8_t> from_config;
    for(auto& part : r_string_utils::split(audio_codec_parameters, ","))
    {
        auto eq = part.find('=');
        if(eq == string::npos) continue;
        auto key = r_string_utils::strip(part.substr(0, eq));
        auto val = r_string_utils::strip(part.substr(eq + 1));

        // Primary: FFmpeg-style base64-encoded Audio Specific Config written by build_audio_codec_params.
        if(key == "sc_asc")
            return r_string_utils::from_base64(val);

        // Fallback: SDP fmtp config= attribute — hex-encoded AAC ASC from RTSP stream metadata.
        if(key == "config" && !val.empty() && (val.size() % 2) == 0 && from_config.empty())
        {
            bool valid = true;
            vector<uint8_t> bytes(val.size() / 2);
            for(size_t i = 0; i < bytes.size() && valid; ++i)
            {
                auto hi = val[i * 2];
                auto lo = val[i * 2 + 1];
                auto hex_nibble = [](char c) -> int {
                    if(c >= '0' && c <= '9') return c - '0';
                    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
                    return -1;
                };
                int h = hex_nibble(hi), l = hex_nibble(lo);
                if(h < 0 || l < 0) { valid = false; break; }
                bytes[i] = (uint8_t)((h << 4) | l);
            }
            if(valid)
                from_config = move(bytes);
        }
    }
    return from_config;
}
