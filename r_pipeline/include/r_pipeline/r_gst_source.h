
#ifndef r_pipeline_r_gst_source_h
#define r_pipeline_r_gst_source_h

#include "r_pipeline/r_arg.h"
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_sample_context.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include "r_gst_buffer.h"
#ifdef IS_WINDOWS
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif
#include <gst/gst.h>
#include <gst/rtsp/gstrtspmessage.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/app/gstappsink.h>
#define GST_USE_UNSTABLE_API
#include <gst/codecparsers/gsth264parser.h>
#include <gst/codecparsers/gsth265parser.h>
#ifdef IS_WINDOWS
#pragma warning( pop )
#endif
#include <vector>
#include <string>
#include <functional>
#include <map>
#include <mutex>

namespace r_pipeline
{

R_API void gstreamer_init();
R_API void gstreamer_deinit();

typedef std::function<void()> r_ready_cb;
typedef std::function<void(const sample_context& ctx, const r_gst_buffer& buffer, bool key, int64_t pts)> r_sample_cb;
typedef std::function<void(const std::map<std::string, r_sdp_media>& sdp_medias)> r_sdp_media_cb;
typedef std::function<void(r_media type, const r_pad_info& pad_info)> r_pad_added_cb;

struct r_camera_params
{
    int64_t bytes_per_second;
    std::map<std::string, r_sdp_media> sdp_medias;
    std::vector<uint8_t> video_key_frame;
};

R_API std::map<std::string, r_sdp_media> fetch_sdp_media(
    const std::string& rtsp_url,
    const r_utils::r_nullable<std::string>& username = r_utils::r_nullable<std::string>(),
    const r_utils::r_nullable<std::string>& password = r_utils::r_nullable<std::string>()
);

R_API int64_t fetch_bytes_per_second(
    const std::string& rtsp_url,
    int measured_duration_seconds = 15,
    const r_utils::r_nullable<std::string>& username = r_utils::r_nullable<std::string>(),
    const r_utils::r_nullable<std::string>& password = r_utils::r_nullable<std::string>()
);

R_API r_camera_params fetch_camera_params(
    const std::string& rtsp_url,
    const r_utils::r_nullable<std::string>& username = r_utils::r_nullable<std::string>(),
    const r_utils::r_nullable<std::string>& password = r_utils::r_nullable<std::string>()
);

class r_gst_caps final
{
public:
    R_API r_gst_caps() : _caps(nullptr) {}
    R_API r_gst_caps(GstCaps* caps) : _caps(caps) {}
    R_API r_gst_caps(const r_gst_caps& obj) = delete;
    R_API r_gst_caps(r_gst_caps&& obj) :
        _caps(std::move(obj._caps))
    {
        obj._caps = nullptr;
    }
    R_API r_gst_caps& operator=(const r_gst_caps&) = delete;
    R_API r_gst_caps& operator=(r_gst_caps&& obj)
    {
        _clear();

        _caps = std::move(obj._caps);
        obj._caps = nullptr;

        return *this;
    }
    R_API ~r_gst_caps() noexcept
    {
        _clear();
    }

    R_API operator GstCaps*() const {return _caps;}

private:
    void _clear() noexcept
    {
        if(_caps)
        {
            gst_caps_unref(_caps);
            _caps = nullptr;
        }
    }

    GstCaps* _caps;
};

enum class H264_NT : uint32_t 
{
    UNKNOWN      = 1,
    SLICE        = 2,
    SLICE_DPA    = 4,
    SLICE_DPB    = 8,
    SLICE_DPC    = 16,
    SLICE_IDR    = 32,
    SEI          = 64,
    SPS          = 128,
    PPS          = 256,
    AU_DELIMITER = 512,
    SEQ_END      = 1024,
    STREAM_END   = 2048,
    FILLER_DATA  = 4096,
    SPS_EXT      = 8192,
    PREFIX_UNIT  = 16384,
    SUBSET_SPS   = 32768,
    DEPTH_SPS    = 65536,
    SLICE_AUX    = 131072,
    SLICE_EXT    = 262144,
    SLICE_DEPTH  = 524288
};

enum class H265_NT : uint32_t 
{
    SLICE_TRAIL_N    = 1,
    SLICE_TRAIL_R    = 2,
    SLICE_TSA_N      = 4,
    SLICE_TSA_R      = 8,
    SLICE_STSA_N     = 16,
    SLICE_STSA_R     = 32,
    SLICE_RADL_N     = 64,
    SLICE_RADL_R     = 128,
    SLICE_RASL_N     = 256,
    SLICE_RASL_R     = 512,
    SLICE_BLA_W_LP   = 1024,
    SLICE_BLA_W_RADL = 2048,
    SLICE_BLA_N_LP   = 4096,
    SLICE_IDR_W_RADL = 8192,
    SLICE_IDR_N_LP   = 16384,
    SLICE_CRA_NUT    = 32768,
    VPS              = 65536,
    SPS              = 131072,
    PPS              = 262144,
    AUD              = 524288,
    EOS              = 1048576,
    EOB              = 2097152,
    FD               = 4194304,
    PREFIX_SEI       = 8388608,
    SUFFIX_SEI       = 16777216
};

class r_gst_source
{
public:
    R_API r_gst_source(const std::string& name_prefix = "revere_");
    R_API r_gst_source(const r_gst_source&) = delete;
    R_API r_gst_source(r_gst_source&&) = delete;
    R_API ~r_gst_source() noexcept;

    R_API r_gst_source& operator=(const r_gst_source&) = delete;
    R_API r_gst_source& operator=(r_gst_source&&) = delete;

    R_API void set_args(const std::vector<r_arg>& args);
    R_API void play();
    R_API void stop();
    R_API bool running() const;

    R_API void set_video_sample_cb(r_sample_cb cb) {_video_sample_cb = cb;}
    R_API void set_audio_sample_cb(r_sample_cb cb) {_audio_sample_cb = cb;}
    R_API void set_ready_cb(r_ready_cb cb) {_ready_cb = cb;}
    R_API void set_sdp_media_cb(r_sdp_media_cb cb) {_sdp_media_cb = cb;}
    R_API void set_pad_added_cb(r_pad_added_cb cb) {_pad_added_cb = cb;}

    // valid after ready callback
    R_API r_utils::r_nullable<r_gst_caps> get_video_caps() const;
    R_API r_utils::r_nullable<r_gst_caps> get_audio_caps() const;

private:
    static GstFlowReturn _new_video_sample(GstElement* elt, r_gst_source* src);
    static GstFlowReturn _new_audio_sample(GstElement* elt, r_gst_source* src);

    static gboolean _select_stream_callbackS(GstElement* src, guint num, GstCaps* caps, r_gst_source* context);
    gboolean _select_stream_callback(GstElement* src, guint num, GstCaps* caps);

    static void _pad_added_callbackS(GstElement* src, GstPad* new_pad, r_gst_source* context);
    void _pad_added_callback(GstElement* src, GstPad* new_pad);

    void _attach_h264_video_pipeline(GstPad* new_pad);
    void _attach_h265_video_pipeline(GstPad* new_pad);
    void _attach_aac_audio_pipeline(GstPad* new_pad, r_encoding encoding);
    void _attach_mulaw_audio_pipeline(GstPad* new_pad);
    void _attach_alaw_audio_pipeline(GstPad* new_pad);
    void _attach_g726_audio_pipeline(GstPad* new_pad);

    static gboolean _bus_callbackS(GstBus* bus, GstMessage* message, gpointer data);
    gboolean _bus_callback(GstBus* bus, GstMessage* message);

    static void _on_sdp_callbackS(GstElement* src, GstSDPMessage* sdp, gpointer data);
    void _on_sdp_callback(GstElement* src, GstSDPMessage* sdp);

    uint32_t _parse_h264(GstH264NalParser* parser, const uint8_t* p, size_t size);
    uint32_t _parse_h265(GstH265Parser* parser, const uint8_t* p, size_t size);

    bool _is_h264_picture(uint32_t ft);
    bool _is_h265_picture(uint32_t ft);

    void _parse_audio_sink_caps();

    void _sei_ts_hack(GstBuffer* buffer, bool& has_pts, bool is_picture, uint64_t& sample_pts);

    void _clear() noexcept;

    std::string _name_prefix;

    std::mutex _sample_cb_lock;
    r_utils::r_nullable<r_sample_cb> _video_sample_cb;
    r_utils::r_nullable<r_sample_cb> _audio_sample_cb;
    r_utils::r_nullable<r_ready_cb> _ready_cb;
    r_utils::r_nullable<r_sdp_media_cb> _sdp_media_cb;
    r_utils::r_nullable<r_pad_added_cb> _pad_added_cb;

    std::map<std::string, r_utils::r_nullable<std::string>> _args;

    /// required arguments
    r_utils::r_nullable<std::string> _url;
    r_utils::r_nullable<std::string> _username;
    r_utils::r_nullable<std::string> _password;

    /// optional arguments
    r_utils::r_nullable<std::string> _protocols;

    GstElement* _pipeline;
    guint _bus_watch_id;
    GstElement* _v_appsink;
    GstElement* _a_appsink;

    mutable std::mutex _state_lok;
    bool _running;

    GstH264NalParser* _h264_nal_parser;
    GstH265Parser* _h265_nal_parser;

    sample_context _sample_context;
    bool _sample_sent;
    bool _video_sample_sent;
    bool _audio_sample_sent;

    bool _buffered_ts;
    uint64_t _buffered_ts_value;

    bool _last_v_pts_valid;
    int64_t _last_v_pts;
    bool _last_a_pts_valid;
    int64_t _last_a_pts;
};

}

#endif
