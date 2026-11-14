#include "r_vss/r_opus_transcoder.h"
#include "r_vss/r_stream_keeper.h"
#include "r_av/r_audio_decoder.h"
#include "r_pipeline/r_stream_info.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_logger.h"
#include <gst/gst.h>

using namespace std;

namespace
{

// key's value in stored codec params ("a=1, b=2"), or empty.
string _codec_param(const string& codec_params, const string& key)
{
    for(auto& part : r_utils::r_string_utils::split(codec_params, ","))
    {
        auto eq = part.find('=');
        if(eq == string::npos)
            continue;
        if(r_utils::r_string_utils::strip(part.substr(0, eq)) == key)
            return r_utils::r_string_utils::strip(part.substr(eq + 1));
    }
    return string();
}

}

namespace r_vss
{

vector<uint8_t> rtp_packet_payload(const uint8_t* p, size_t size)
{
    if(!p || size < 12 || (p[0] >> 6) != 2)
        return {};
    size_t at = 12 + 4 * (size_t)(p[0] & 0x0F);
    if(p[0] & 0x10)
    {
        if(at + 4 > size)
            return {};
        at += 4 + 4 * (((size_t)p[at + 2] << 8) | p[at + 3]);
    }
    size_t end = size;
    if(p[0] & 0x20)
    {
        if(end == 0 || p[end - 1] == 0 || p[end - 1] > end)
            return {};
        end -= p[end - 1];
    }
    if(at >= end)
        return {};
    return vector<uint8_t>(p + at, p + end);
}

struct r_opus_transcoder::impl
{
    GstElement* pipeline {nullptr};
    GstElement* src {nullptr};
    GstElement* sink {nullptr};
    unique_ptr<r_av::r_audio_decoder> aac;   // AAC: decoded here first
    bool have_first {false};
    int64_t first_ms {0};
    uint64_t pcm_samples {0};                // AAC: PCM pushed so far, for its times

    ~impl()
    {
        if(pipeline)
        {
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
        }
        if(src)
            gst_object_unref(src);
        if(sink)
            gst_object_unref(sink);
    }
};

r_opus_transcoder::r_opus_transcoder(const string& codec_name, const string& codec_params, uint32_t frame_ms) :
    _impl(make_unique<impl>()),
    _frame_ms(frame_ms == 40 || frame_ms == 60 ? frame_ms : 20)
{
    if(!gst_is_initialized())
        gst_init(nullptr, nullptr);

    r_pipeline::r_encoding enc;
    try
    {
        enc = r_pipeline::str_to_encoding(codec_name);
    }
    catch(...)
    {
        _why_not = "the camera's audio is " + codec_name;
        return;
    }
    auto branch = restream_opus_audio_branch(enc);
    if(branch.empty())
    {
        _why_not = "no Opus branch for " + codec_name + " (or GStreamer lacks an element it needs)";
        return;
    }
    // Revere's branch makes 20 ms frames; Apple Home may ask for longer packets.
    auto fs = branch.find("frame-size=20");
    if(fs != string::npos && _frame_ms != 20)
        branch.replace(fs, 13, "frame-size=" + to_string(_frame_ms));

    string caps;
    if(enc == r_pipeline::r_encoding::AAC_GENERIC_ENCODING)
    {
        auto asc = r_pipeline::get_audio_codec_extradata(codec_params);
        if(asc.empty())
        {
            _why_not = "the camera's AAC has no AudioSpecificConfig";
            return;
        }
        _impl->aac = make_unique<r_av::r_audio_decoder>(AV_CODEC_ID_AAC);
        _impl->aac->set_extradata(asc);
        caps = R_RESTREAM_PCM_CAPS;
    }
    else
    {
        auto b64 = _codec_param(codec_params, "encoded_audio_sample_caps");
        auto decoded = b64.empty() ? vector<uint8_t>() : r_utils::r_string_utils::from_base64(b64);
        caps.assign(decoded.begin(), decoded.end());
        if(caps.empty())
        {
            _why_not = "the camera's audio has no stored caps";
            return;
        }
    }

    auto launch = "appsrc name=src format=time is-live=true ! " + branch + " ! appsink name=sink sync=false max-buffers=200 drop=true";
    GError* err = nullptr;
    _impl->pipeline = gst_parse_launch(launch.c_str(), &err);
    if(err)
    {
        _why_not = string("the Opus pipeline: ") + err->message;
        g_error_free(err);
        return;
    }
    _impl->src = gst_bin_get_by_name(GST_BIN(_impl->pipeline), "src");
    _impl->sink = gst_bin_get_by_name(GST_BIN(_impl->pipeline), "sink");
    auto* c = gst_caps_from_string(caps.c_str());
    if(!_impl->src || !_impl->sink || !c)
    {
        if(c)
            gst_caps_unref(c);
        _why_not = "the Opus pipeline couldn't be built";
        return;
    }
    g_object_set(G_OBJECT(_impl->src), "caps", c, NULL);
    gst_caps_unref(c);
    if(gst_element_set_state(_impl->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        _why_not = "the Opus pipeline wouldn't start";
        return;
    }
    _ok = true;
}

r_opus_transcoder::~r_opus_transcoder() noexcept = default;

void r_opus_transcoder::push(const uint8_t* data, size_t size, int64_t ts_ms)
{
    if(!_ok || !data || !size)
        return;
    auto& m = *_impl;
    vector<uint8_t> pcm;
    if(m.aac)
    {
        pcm = restream_pcm(*m.aac, data, size);
        if(pcm.empty())
            return;
        data = pcm.data();
        size = pcm.size();
    }
    if(!m.have_first)
    {
        m.have_first = true;
        m.first_ms = ts_ms;
    }
    auto* buf = gst_buffer_new_allocate(nullptr, size, nullptr);
    gst_buffer_fill(buf, 0, data, size);
    if(m.aac)
    {
        // PCM's times from the samples, so a decoder's delay doesn't jitter them.
        GST_BUFFER_PTS(buf) = gst_util_uint64_scale(m.pcm_samples, GST_SECOND, 16000);
        m.pcm_samples += size / 2;
        GST_BUFFER_DURATION(buf) = gst_util_uint64_scale(size / 2, GST_SECOND, 16000);
    }
    else
        GST_BUFFER_PTS(buf) = (GstClockTime)(ts_ms > m.first_ms ? ts_ms - m.first_ms : 0) * GST_MSECOND;
    GstFlowReturn ret = GST_FLOW_OK;
    g_signal_emit_by_name(m.src, "push-buffer", buf, &ret);
    gst_buffer_unref(buf);
    if(ret != GST_FLOW_OK)
        R_LOG_WARNING("Opus transcoder: the pipeline refused audio (%d)", (int)ret);
}

vector<vector<uint8_t>> r_opus_transcoder::take(int timeout_ms)
{
    vector<vector<uint8_t>> out;
    if(!_ok)
        return out;
    GstClockTime wait = (GstClockTime)(timeout_ms > 0 ? timeout_ms : 0) * GST_MSECOND;
    while(true)
    {
        GstSample* sample = nullptr;
        g_signal_emit_by_name(_impl->sink, "try-pull-sample", out.empty() ? wait : (GstClockTime)0, &sample);
        if(!sample)
            break;
        auto* buf = gst_sample_get_buffer(sample);
        GstMapInfo map;
        if(buf && gst_buffer_map(buf, &map, GST_MAP_READ))
        {
            auto payload = rtp_packet_payload(map.data, map.size);
            if(!payload.empty())
                out.push_back(std::move(payload));
            gst_buffer_unmap(buf, &map);
        }
        gst_sample_unref(sample);
    }
    return out;
}

}
