#include "r_disco/r_substream.h"
#include "r_pipeline/r_stream_info.h"
#include "r_utils/r_logger.h"
#include <algorithm>
#include <exception>

using namespace r_disco;
using namespace std;

r_substream_choice r_disco::find_substream(
    const vector<r_onvif::onvif_profile_info>& profiles,
    const r_onvif::onvif_profile_info& main,
    uint16_t main_width,
    uint16_t main_height,
    const r_substream_camera& camera
)
{
    r_substream_choice choice;

    auto candidates = r_onvif::substream_candidates(profiles, main);
    if(candidates.empty())
    {
        choice.reason = "no smaller stream";
        return choice;
    }

    auto tries = min(candidates.size(), (size_t)SUBSTREAM_MAX_PROBES);
    for(size_t i = 0; i < tries; ++i)
    {
        auto& candidate = candidates[i];

        string url;
        try
        {
            url = camera.stream_uri(candidate.token);
        }
        catch(const exception& e)
        {
            R_LOG_INFO("substream: no stream address for profile %s: %s", candidate.token.c_str(), e.what());
        }

        string reason;
        if(url.empty())
            reason = "no stream address";
        else
        {
            auto probe = camera.probe(url);
            auto check = r_pipeline::check_substream(probe, main_width, main_height);
            if(check.ok)
            {
                choice.found = true;
                choice.reason.clear();
                choice.profile = candidate;
                choice.rtsp_url = url;
                choice.probe = probe;
                choice.anamorphic = check.anamorphic;
                return choice;
            }
            reason = check.reason;
        }

        // The best candidate's reason is the one shown.
        if(choice.reason.empty())
            choice.reason = reason;
    }

    return choice;
}

pair<uint16_t, uint16_t> r_disco::main_picture_size(const string& codec, const string& parameters)
{
    try
    {
        if(codec == "H264")
        {
            auto sps = r_pipeline::get_h264_sps(parameters);
            if(!sps.is_null())
            {
                auto info = r_pipeline::parse_h264_sps(sps.value());
                return {info.width, info.height};
            }
        }
        else if(codec == "H265")
        {
            auto sps = r_pipeline::get_h265_sps(parameters);
            if(!sps.is_null())
            {
                auto info = r_pipeline::parse_h265_sps(sps.value());
                return {info.width, info.height};
            }
        }
    }
    catch(const exception& e)
    {
        R_LOG_INFO("substream: main's SPS didn't parse: %s", e.what());
    }
    return {0, 0};
}

void r_disco::substream_set(
    r_camera& camera,
    const r_substream_choice& choice,
    const string& codec,
    const string& parameters,
    int timebase
)
{
    camera.sub_mode.set_value("on");
    camera.sub_status.set_value("ok");
    camera.sub_onvif_profile_token.set_value(choice.profile.token);
    camera.sub_rtsp_url.set_value(choice.rtsp_url);
    camera.sub_video_codec.set_value(codec);
    camera.sub_video_codec_parameters.set_value(parameters);
    camera.sub_video_timebase.set_value(timebase);
    // The picture the stream really has (its SPS), not what ONVIF said.
    camera.sub_width.set_value(choice.probe.width);
    camera.sub_height.set_value(choice.probe.height);
    if(choice.probe.key_interval_ms >= 0)
        camera.sub_key_interval_ms.set_value((int)choice.probe.key_interval_ms);
    else
        camera.sub_key_interval_ms.clear();
}

void r_disco::substream_clear(r_camera& camera, const string& reason)
{
    camera.sub_mode.set_value("off");
    camera.sub_status.set_value("none: " + reason);
    camera.sub_onvif_profile_token.clear();
    camera.sub_rtsp_url.clear();
    camera.sub_video_codec.clear();
    camera.sub_video_codec_parameters.clear();
    camera.sub_video_timebase.clear();
    camera.sub_width.clear();
    camera.sub_height.clear();
    camera.sub_key_interval_ms.clear();
}

void r_disco::substream_copy(const r_camera& from, r_camera& to)
{
    to.sub_mode = from.sub_mode;
    to.sub_status = from.sub_status;
    to.sub_onvif_profile_token = from.sub_onvif_profile_token;
    to.sub_rtsp_url = from.sub_rtsp_url;
    to.sub_video_codec = from.sub_video_codec;
    to.sub_video_codec_parameters = from.sub_video_codec_parameters;
    to.sub_video_timebase = from.sub_video_timebase;
    to.sub_width = from.sub_width;
    to.sub_height = from.sub_height;
    to.sub_key_interval_ms = from.sub_key_interval_ms;
}

bool r_disco::substream_follow(r_camera& camera, const string& url, const string& codec, const string& parameters)
{
    if(!substream_on(camera))
        return true;

    // A description that doesn't carry the SPS has no size to compare (0 x 0): that isn't a change.
    auto size = main_picture_size(codec, parameters);
    const bool size_known = size.first > 0 && size.second > 0;
    const bool same =
        !camera.sub_video_codec.is_null() && camera.sub_video_codec.value() == codec &&
        (!size_known || (!camera.sub_width.is_null() && camera.sub_width.value() == size.first &&
                         !camera.sub_height.is_null() && camera.sub_height.value() == size.second));
    if(!same)
    {
        substream_clear(camera, "camera changed the stream");
        return false;
    }

    camera.sub_rtsp_url.set_value(url);
    // Parameters without the SPS don't replace ones that have it.
    if(size_known || camera.sub_video_codec_parameters.is_null())
        camera.sub_video_codec_parameters.set_value(parameters);
    return true;
}

size_t r_disco::main_profile_index(const vector<r_onvif::onvif_profile_info>& profiles, const r_utils::r_nullable<string>& token)
{
    if(!token.is_null())
    {
        for(size_t i = 0; i < profiles.size(); ++i)
        {
            if(profiles[i].token == token.value())
                return i;
        }
    }
    size_t best_idx = 0;
    uint32_t best = 0;
    for(size_t i = 0; i < profiles.size(); ++i)
    {
        uint32_t pixels = (uint32_t)profiles[i].width * (uint32_t)profiles[i].height;
        if(pixels > best)
        {
            best = pixels;
            best_idx = i;
        }
    }
    return best_idx;
}

string r_disco::stream_title(const string& codec, int width, int height)
{
    string name;
    if(codec == "H264")
        name = "H.264";
    else if(codec == "H265")
        name = "H.265";
    else
    {
        name = codec;
        for(auto& ch : name)
            ch = (char)toupper((unsigned char)ch);
    }
    string size;
    if(width > 0 && height > 0)
        size = to_string(width) + " \xC3\x97 " + to_string(height);
    if(name.empty())
        return size;
    return size.empty() ? name : name + "  \xC2\xB7  " + size;
}

string r_disco::substream_title(const r_camera& camera)
{
    return stream_title(
        camera.sub_video_codec.is_null() ? string() : camera.sub_video_codec.value(),
        camera.sub_width.is_null() ? 0 : camera.sub_width.value(),
        camera.sub_height.is_null() ? 0 : camera.sub_height.value()
    );
}

string r_disco::substream_none_reason(const r_camera& camera)
{
    if(camera.sub_status.is_null())
        return string();
    const auto& status = camera.sub_status.value();
    if(status.rfind("none:", 0) != 0)
        return string();
    auto reason = status.substr(5);
    reason.erase(0, reason.find_first_not_of(' '));
    return reason;
}
