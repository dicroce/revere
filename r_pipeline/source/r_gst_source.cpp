
#include "r_pipeline/r_gst_source.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_std_utils.h"
#include "r_utils/r_work_q.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_file.h"
#include <gst/rtsp/gstrtsptransport.h>
#include <memory>
#include <chrono>
#include <thread>

using namespace r_pipeline;
using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace std;
using namespace std::chrono;

static const int MSECS_IN_TEN_MINUTES = 600000;

void r_pipeline::gstreamer_init()
{
    gst_init(NULL, NULL);

    GstRegistry *registry = gst_registry_get();
    auto wd_plugins = r_fs::path_join(r_fs::working_directory(), "gstreamer_plugins");
    if(r_fs::file_exists(wd_plugins))
    {
        R_LOG_INFO("Loading plugins from: %s", wd_plugins.c_str());
        gst_registry_scan_path(registry, wd_plugins.c_str());
    }
    else R_LOG_INFO("No plugins found in: %s", wd_plugins.c_str());
}

void r_pipeline::gstreamer_deinit()
{
    gst_deinit();
}

map<string, r_sdp_media> r_pipeline::fetch_sdp_media(
    const string& rtsp_url,
    const r_nullable<string>& username,
    const r_nullable<string>& password
)
{
    vector<r_arg> arguments;
    add_argument(arguments, "url", rtsp_url);

    if(!username.is_null())
        add_argument(arguments, "username", username.value());

    if(!password.is_null())
        add_argument(arguments, "password", password.value());

    r_gst_source src;
    src.set_args(arguments);

    r_work_q<int, void> q;
    map<string, r_sdp_media> medias;
    src.set_sdp_media_cb([&](const map<string, r_sdp_media>& sdp_medias){
        medias = sdp_medias;
    });
    src.set_video_sample_cb([&](const sample_context& sc, const r_gst_buffer&, bool key, int64_t pts){
        q.post(42);
    });

    src.play();

    auto res = q.poll(seconds(10));

    src.stop();

    return medias;
}

int64_t r_pipeline::fetch_bytes_per_second(
    const std::string& rtsp_url,
    int measured_duration_seconds,
    const r_utils::r_nullable<std::string>& username,
    const r_utils::r_nullable<std::string>& password
)
{
    vector<r_arg> arguments;
    add_argument(arguments, "url", rtsp_url);

    if(!username.is_null())
        add_argument(arguments, "username", username.value());

    if(!password.is_null())
        add_argument(arguments, "password", password.value());

    r_gst_source src;
    src.set_args(arguments);

    bool done = false;

    int64_t audio_byte_total = 0;
    src.set_audio_sample_cb([&](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts){
        auto mi = buffer.map(r_gst_buffer::MT_READ);
        if(!done)
            audio_byte_total += mi.size();
    });

    bool stream_start_time_set = false;
    system_clock::time_point stream_start_time;    

    int64_t video_byte_total = 0;
    src.set_video_sample_cb([&](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts){
        auto mi = buffer.map(r_gst_buffer::MT_READ);
        if(!stream_start_time_set)
        {
            stream_start_time_set = true;
            stream_start_time = system_clock::now();
        }
        if(!done)
            video_byte_total += mi.size();
    });

    src.play();

    std::this_thread::sleep_for(std::chrono::seconds(measured_duration_seconds));

    done = true;

    auto delta = system_clock::now() - stream_start_time;

    src.stop();

    return (int64_t)(((double)(audio_byte_total + video_byte_total)) / (double)duration_cast<seconds>(delta).count());
}

r_camera_params r_pipeline::fetch_camera_params(
    const std::string& rtsp_url,
    const r_utils::r_nullable<std::string>& username,
    const r_utils::r_nullable<std::string>& password
)
{
    vector<r_arg> arguments;
    add_argument(arguments, "url", rtsp_url);

    if(!username.is_null())
        add_argument(arguments, "username", username.value());

    if(!password.is_null())
        add_argument(arguments, "password", password.value());

    r_gst_source src;
    src.set_args(arguments);

    bool done = false;

    map<string, r_sdp_media> medias;
    src.set_sdp_media_cb([&](const map<string, r_sdp_media>& sdp_medias){
        medias = sdp_medias;
    });

    int64_t audio_byte_total = 0;
    src.set_audio_sample_cb([&](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts){
        auto mi = buffer.map(r_gst_buffer::MT_READ);
        if(!done)
            audio_byte_total += mi.size();
    });

    bool stream_start_time_set = false;
    system_clock::time_point stream_start_time;    

    vector<uint8_t> video_key_frame;

    int64_t video_byte_total = 0;
    src.set_video_sample_cb([&](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts){
        auto mi = buffer.map(r_gst_buffer::MT_READ);
        if(!stream_start_time_set)
        {
            stream_start_time_set = true;
            stream_start_time = system_clock::now();

            video_key_frame.resize(mi.size());
            memcpy(&video_key_frame[0], mi.data(), mi.size());
        }

        if(!done)
            video_byte_total += mi.size();
    });

    src.play();

    std::this_thread::sleep_for(std::chrono::seconds(15));

    done = true;

    auto delta = system_clock::now() - stream_start_time;

    src.stop();

    r_camera_params cp;
    cp.bytes_per_second = (int64_t)(((double)(audio_byte_total + video_byte_total)) / (double)duration_cast<seconds>(delta).count());
    cp.sdp_medias = medias;
    cp.video_key_frame = video_key_frame;
    return cp;
}

r_gst_source::r_gst_source(const string& name_prefix) :
    _name_prefix(name_prefix),
    _sample_cb_lock(),
    _video_sample_cb(),
    _audio_sample_cb(),
    _ready_cb(),
    _sdp_media_cb(),
    _pad_added_cb(),
    _args(),
    _url(),
    _username(),
    _password(),
    _protocols(),
    _pipeline(nullptr),
    _bus_watch_id(0),
    _v_appsink(nullptr),
    _a_appsink(nullptr),
    _state_lok(),
    _running(false),
    _h264_nal_parser(nullptr),
    _h265_nal_parser(nullptr),
    _sample_context(),
    _sample_sent(false),
    _video_sample_sent(false),
    _audio_sample_sent(false),
    _buffered_ts(false),
    _buffered_ts_value(0),
    _last_v_pts_valid(false),
    _last_v_pts(0),
    _last_a_pts_valid(false),
    _last_a_pts(0)
{
}

r_gst_source::~r_gst_source() noexcept
{
    _clear();
}

void r_gst_source::set_args(const vector<r_arg>& args)
{
    _args.clear();
    for(auto arg: args)
        _args[arg.get_name()] = arg.get_value();

    _url = _args["url"];
    if(_url.is_null())
        R_STHROW(r_invalid_argument_exception, ("required argument not found: url"));

    if(_args.count("protocols") > 0)
        _protocols = _args["protocols"];

    _username = _args["username"];
    _password = _args["password"];
}

void r_gst_source::play()
{
    lock_guard<mutex> g(_state_lok);

    GstElement* rtspsrc = gst_element_factory_make("rtspsrc", "src");

    if(!_protocols.is_null())
    {
        g_object_set(G_OBJECT(rtspsrc), "protocols", 0x00000004, NULL);
        g_object_set(G_OBJECT(rtspsrc), "buffer-mode", 4, NULL);
        g_object_set(G_OBJECT(rtspsrc), "latency", 50, NULL);
    }

    g_object_set(G_OBJECT(rtspsrc), "do-rtsp-keep-alive", true, NULL);
    g_object_set(G_OBJECT(rtspsrc), "location", _url.value().c_str(), NULL);

    if(!_username.is_null())
        g_object_set(G_OBJECT(rtspsrc), "user-id", _username.value().c_str(), NULL);
    if(!_password.is_null())
        g_object_set(G_OBJECT(rtspsrc), "user-pw", _password.value().c_str(), NULL);

    g_signal_connect(G_OBJECT(rtspsrc), "select-stream", G_CALLBACK(_select_stream_callbackS), this);

    g_signal_connect(G_OBJECT(rtspsrc), "pad-added", G_CALLBACK(_pad_added_callbackS), this);

    g_signal_connect(G_OBJECT(rtspsrc), "on-sdp", G_CALLBACK(_on_sdp_callbackS), this);

    _pipeline = gst_pipeline_new((_name_prefix + "pipeline").c_str());

    auto added = gst_bin_add(GST_BIN(_pipeline), rtspsrc);

    raii_ptr<GstBus> bus(
        gst_pipeline_get_bus(GST_PIPELINE(_pipeline)),
        [](GstBus* bus){gst_object_unref(bus);}
    );

    _bus_watch_id = gst_bus_add_watch(bus.get(), _bus_callbackS, this);

    gst_element_set_state(_pipeline, GST_STATE_PLAYING);

    _running = true;
}

void r_gst_source::stop()
{
    lock_guard<mutex> g(_state_lok);

    if(_pipeline && _running)
    {
        auto result = gst_element_set_state(_pipeline, GST_STATE_NULL);

        auto done = false;
        while(!done)
        {
            if(result == GST_STATE_CHANGE_FAILURE)
                R_THROW(("Unable to stop the pipeline."));
            else if(result == GST_STATE_CHANGE_SUCCESS || result == GST_STATE_CHANGE_NO_PREROLL)
                done = true;
            else result = gst_element_get_state(_pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
        }

        _running = false;
    }
}

bool r_gst_source::running() const
{
    lock_guard<mutex> g(_state_lok);

    return _running;
}

r_nullable<r_gst_caps> r_gst_source::get_video_caps() const
{
    r_nullable<r_gst_caps> maybe_caps;
    auto sink_pad = gst_element_get_static_pad(_v_appsink, "sink");
    if(sink_pad)
    {
        auto caps = gst_pad_get_current_caps(sink_pad);
        if(caps)
            maybe_caps.assign(r_gst_caps(caps));
    }
    return maybe_caps;
}

r_nullable<r_gst_caps> r_gst_source::get_audio_caps() const
{
    r_nullable<r_gst_caps> maybe_caps;
    auto sink_pad = gst_element_get_static_pad(_a_appsink, "sink");
    if(sink_pad)
    {
        auto caps = gst_pad_get_current_caps(sink_pad);
        if(caps)
            maybe_caps.assign(r_gst_caps(caps));
    }
    return maybe_caps;
}

#if 0
// Here are some functions that I sometimes use for debugging. I'm leaving them here so I can easily
// use them in the future.
static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) {
  gchar *str = gst_value_serialize (value);

  g_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}

static void print_caps (const GstCaps * caps, const gchar * pfx) {
  guint i;

  g_return_if_fail (caps != NULL);

  if (gst_caps_is_any (caps)) {
    g_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    g_print ("%sEMPTY\n", pfx);
    return;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    g_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}

static string nal_type(int type)
{
  if(type == GST_H264_NAL_UNKNOWN)
    return "GST_H264_NAL_UNKNOWN";
  else if(type == GST_H264_NAL_SLICE)
    return "GST_H264_NAL_SLICE";
  else if(type == GST_H264_NAL_SLICE_DPA)
      return "GST_H264_NAL_SLICE_DPA";
  else if(type == GST_H264_NAL_SLICE_DPB)
      return "GST_H264_NAL_SLICE_DPB";
  else if(type == GST_H264_NAL_SLICE_DPC)
      return "GST_H264_NAL_SLICE_DPC";
  else if(type == GST_H264_NAL_SLICE_IDR)
      return "GST_H264_NAL_SLICE_IDR";
  else if(type == GST_H264_NAL_SEI)
      return "GST_H264_NAL_SEI";
  else if(type == GST_H264_NAL_SPS)
      return "GST_H264_NAL_SPS";
  else if(type == GST_H264_NAL_PPS)
      return "GST_H264_NAL_PPS";
  else if(type == GST_H264_NAL_AU_DELIMITER)
      return "GST_H264_NAL_AU_DELIMITER";
  else if(type == GST_H264_NAL_SEQ_END)
      return "GST_H264_NAL_SEQ_END";
  else if(type == GST_H264_NAL_STREAM_END)
      return "GST_H264_NAL_STREAM_END";
  else if(type == GST_H264_NAL_FILLER_DATA)
      return "GST_H264_NAL_FILLER_DATA";
  else if(type == GST_H264_NAL_SPS_EXT)
      return "GST_H264_NAL_SPS_EXT";
  else if(type == GST_H264_NAL_PREFIX_UNIT)
      return "GST_H264_NAL_PREFIX_UNIT";
  else if(type == GST_H264_NAL_SUBSET_SPS)
      return "GST_H264_NAL_SUBSET_SPS";
  else if(type == GST_H264_NAL_DEPTH_SPS)
      return "GST_H264_NAL_DEPTH_SPS";
  else if(type == GST_H264_NAL_SLICE_AUX)
      return "GST_H264_NAL_SLICE_AUX";
  else if(type == GST_H264_NAL_SLICE_EXT)
      return "";
  else if(type == GST_H264_NAL_SLICE_DEPTH)
      return "GST_H264_NAL_SLICE_DEPTH";
    R_THROW(("UNKNOWN NAL"));
}
#endif

gboolean r_gst_source::_select_stream_callbackS(GstElement* src, guint num, GstCaps* caps, r_gst_source* context)
{
    try
    {
        return context->_select_stream_callback(src, num, caps);
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
        g_set_error(NULL, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, "pad added callback error");
    }
    return FALSE;
}

gboolean r_gst_source::_select_stream_callback(GstElement* src, guint num, GstCaps* caps)
{
    GstStructure* new_pad_struct = gst_caps_get_structure(caps, 0);

    auto media_str = string(gst_structure_get_string(new_pad_struct, "media"));

    if(media_str == "video")
        return TRUE;
    else if(media_str == "audio")
        return TRUE;

    return FALSE;
}

void r_gst_source::_pad_added_callbackS(GstElement* src, GstPad* new_pad, r_gst_source* context)
{
    try
    {
        context->_pad_added_callback(src, new_pad);
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
        g_set_error(NULL, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, "pad added callback error");
    }
}

static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) {
  gchar *str = gst_value_serialize (value);

  g_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}

static void print_caps (const GstCaps * caps, const gchar * pfx) {
  guint i;

  g_return_if_fail (caps != NULL);

  if (gst_caps_is_any (caps)) {
    g_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    g_print ("%sEMPTY\n", pfx);
    return;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    g_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}

void r_gst_source::_pad_added_callback(GstElement* src, GstPad* new_pad)
{
    raii_ptr<GstCaps> new_pad_caps(
        gst_pad_get_current_caps(new_pad),
        [](GstCaps* caps){gst_caps_unref(caps);}
    );

    GstStructure* new_pad_struct = gst_caps_get_structure(new_pad_caps.get(), 0);

    print_caps(new_pad_caps.get(), "      ");

    const gchar* new_pad_type = gst_structure_get_name(new_pad_struct);

    auto temp_s = gst_structure_get_string(new_pad_struct, "media");

    auto media_str = (temp_s)?r_string_utils::to_lower(string(temp_s)):string();

    if(g_str_has_prefix(new_pad_type, "application/x-rtp") && (media_str == "video" || media_str == "audio"))
    {
        r_pad_info si;

        si.media = (media_str == "video")?VIDEO_MEDIA:AUDIO_MEDIA;

        si.payload = 0;
        gst_structure_get_int(new_pad_struct, "payload", &si.payload);
        si.clock_rate = 0;
        gst_structure_get_int(new_pad_struct, "clock-rate", &si.clock_rate);

        if(gst_structure_has_field(new_pad_struct, "a-framerate") == TRUE)
        {
            temp_s = gst_structure_get_string(new_pad_struct, "a-framerate");
            si.framerate.set_value((temp_s)?r_string_utils::s_to_double(temp_s):0.0);
        }

        bool has_encoding = gst_structure_has_field(new_pad_struct, "encoding-name") == TRUE;

        r_encoding encoding;

        if(has_encoding)
        {
            temp_s = gst_structure_get_string(new_pad_struct, "encoding-name");

            string encoding_name(temp_s?temp_s:string());

            try
            {
                encoding = str_to_encoding(encoding_name);
            }
            catch(const std::exception& e)
            {
                R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
            }
        }

        if(si.media == VIDEO_MEDIA)
        {
            if(has_encoding && encoding == H264_ENCODING)
            {
                r_h264_info h264_info;

                auto str = gst_structure_get_string(new_pad_struct, "profile-level-id");
                h264_info.profile_level_id = (str)?string(str):string();
                str = gst_structure_get_string(new_pad_struct, "sprop-parameter-sets");
                auto sprop_parameter_sets = (str)?string(str):string();
                auto parts = r_string_utils::split(sprop_parameter_sets, ',');
                h264_info.sprop_sps = parts[0];
                h264_info.sprop_pps = parts[1];
                h264_info.packetization_mode = 0;
                gst_structure_get_int(new_pad_struct, "packetization-mode", &h264_info.packetization_mode);

                si.encoding = H264_ENCODING;
                si.h264.set_value(h264_info);

                _attach_h264_video_pipeline(new_pad);

                _h264_nal_parser = gst_h264_nal_parser_new();
            }
            else if(has_encoding && encoding == H265_ENCODING)
            {
                r_h265_info h265_info;
                auto str = gst_structure_get_string(new_pad_struct, "sprop-vps");
                h265_info.sprop_vps = (str)?string(str):string();
                str = gst_structure_get_string(new_pad_struct, "sprop-sps");
                h265_info.sprop_sps = (str)?string(str):string();
                str = gst_structure_get_string(new_pad_struct, "sprop-pps");
                h265_info.sprop_pps = (str)?string(str):string();

                si.encoding = H265_ENCODING;
                si.h265.set_value(h265_info);

                _attach_h265_video_pipeline(new_pad);

                _h265_nal_parser = gst_h265_parser_new();
            }
        }
        else if(si.media == AUDIO_MEDIA)
        {
            r_nullable<int> clock_rate;

            int value;
            if(gst_structure_get_int(new_pad_struct, "clock-rate", &value) == TRUE)
                clock_rate.set_value(value);

            if(has_encoding && encoding == AAC_LATM_ENCODING)
            {
                r_aac_info aac_info;
                aac_info.clock_rate = clock_rate;

                si.encoding = AAC_LATM_ENCODING;
                si.aac.set_value(aac_info);

                _attach_aac_audio_pipeline(new_pad, encoding);
            }
            else if(has_encoding && encoding == AAC_GENERIC_ENCODING)
            {
                r_aac_info aac_info;
                aac_info.clock_rate = clock_rate;

                si.encoding = AAC_GENERIC_ENCODING;
                si.aac.set_value(aac_info);

                _attach_aac_audio_pipeline(new_pad, encoding);
            }
            else if(has_encoding && encoding == PCMU_ENCODING)
            {
                r_pcmu_info pcmu_info;
                pcmu_info.clock_rate = clock_rate;

                si.encoding = PCMU_ENCODING;
                si.pcmu.set_value(pcmu_info);

                _attach_mulaw_audio_pipeline(new_pad);
            }
            else if(has_encoding && encoding == PCMA_ENCODING)
            {
                r_pcma_info pcma_info;
                pcma_info.clock_rate = clock_rate;

                si.encoding = PCMA_ENCODING;
                si.pcma.set_value(pcma_info);

                _attach_alaw_audio_pipeline(new_pad);
            }
            else if(has_encoding && encoding == AAL2_G726_16_ENCODING)
            {
                r_g726_info g726_info;
                g726_info.clock_rate = clock_rate;

                si.encoding = AAL2_G726_16_ENCODING;
                si.g726.set_value(g726_info);

                _attach_g726_audio_pipeline(new_pad);
            }
            else if(has_encoding && encoding == AAL2_G726_24_ENCODING)
            {
                r_g726_info g726_info;
                g726_info.clock_rate = clock_rate;

                si.encoding = AAL2_G726_24_ENCODING;
                si.g726.set_value(g726_info);

                _attach_g726_audio_pipeline(new_pad);
            }
            else if(has_encoding && encoding == AAL2_G726_32_ENCODING)
            {
                r_g726_info g726_info;
                g726_info.clock_rate = clock_rate;

                si.encoding = AAL2_G726_32_ENCODING;
                si.g726.set_value(g726_info);

                _attach_g726_audio_pipeline(new_pad);
            }
            else if(has_encoding && encoding == AAL2_G726_40_ENCODING)
            {
                r_g726_info g726_info;
                g726_info.clock_rate = clock_rate;

                si.encoding = AAL2_G726_40_ENCODING;
                si.g726.set_value(g726_info);

                _attach_g726_audio_pipeline(new_pad);
            }
            else if(has_encoding && encoding == G726_16_ENCODING)
            {
                r_g726_info g726_info;
                g726_info.clock_rate = clock_rate;

                si.encoding = G726_16_ENCODING;
                si.g726.set_value(g726_info);

                _attach_g726_audio_pipeline(new_pad);
            }
            else if(has_encoding && encoding == G726_24_ENCODING)
            {
                r_g726_info g726_info;
                g726_info.clock_rate = clock_rate;

                si.encoding = G726_24_ENCODING;
                si.g726.set_value(g726_info);

                _attach_g726_audio_pipeline(new_pad);
            }
            else if(has_encoding && encoding == G726_32_ENCODING)
            {
                r_g726_info g726_info;
                g726_info.clock_rate = clock_rate;

                si.encoding = G726_32_ENCODING;
                si.g726.set_value(g726_info);

                _attach_g726_audio_pipeline(new_pad);
            }
            else if(has_encoding && encoding == G726_40_ENCODING)
            {
                r_g726_info g726_info;
                g726_info.clock_rate = clock_rate;
                
                si.encoding = G726_40_ENCODING;
                si.g726.set_value(g726_info);

                _attach_g726_audio_pipeline(new_pad);
            }

            if(!has_encoding)
            {
                if(si.payload == 0)
                {
                    r_pcmu_info pcmu_info;
                    pcmu_info.clock_rate = clock_rate;

                    si.encoding = PCMU_ENCODING;
                    si.pcmu.set_value(pcmu_info);

                    _attach_mulaw_audio_pipeline(new_pad);
                }
                else if(si.payload == 8)
                {
                    r_pcma_info pcma_info;
                    pcma_info.clock_rate = clock_rate;

                    si.encoding = PCMA_ENCODING;
                    si.pcma.set_value(pcma_info);

                    _attach_alaw_audio_pipeline(new_pad);
                }
            }
        }

        _sample_context._src_pad_info[si.media] = si;

        if(!_pad_added_cb.is_null())
            _pad_added_cb.value()(si.media, si);
    }
}

void r_gst_source::_attach_h264_video_pipeline(GstPad* new_pad)
{
    GstElement* v_depay = gst_element_factory_make("rtph264depay", (_name_prefix + "rtph264depay").c_str());

    GstElement* v_parser = gst_element_factory_make("h264parse", (_name_prefix + "h264parse").c_str());
    g_object_set(G_OBJECT(v_parser), "config-interval", -1, NULL);

    _v_appsink = gst_element_factory_make("appsink", (_name_prefix + "v_appsink").c_str());

    g_object_set(G_OBJECT(_v_appsink), "emit-signals", TRUE, "sync", FALSE, NULL);

    gst_bin_add_many(GST_BIN(_pipeline), v_depay, v_parser, _v_appsink, NULL);

    gst_element_sync_state_with_parent(v_depay);
    gst_element_sync_state_with_parent(v_parser);
    gst_element_sync_state_with_parent(_v_appsink);

    raii_ptr<GstCaps> depay_filter_caps(
        gst_caps_new_simple(
            "video/x-h264",
            "stream-format", G_TYPE_STRING, "avc",
            "alignment", G_TYPE_STRING, "au",
            NULL
        ),
        [](GstCaps* caps){gst_caps_unref(caps);}
    );

    auto linked = gst_element_link_filtered(v_depay, v_parser, depay_filter_caps.get());

    raii_ptr<GstCaps> parser_filter_caps(
        gst_caps_new_simple(
            "video/x-h264",
            "stream-format", G_TYPE_STRING, "byte-stream",
            "alignment", G_TYPE_STRING, "au",
            NULL
        ),
        [](GstCaps* caps){gst_caps_unref(caps);}
    );

    auto linked2 = gst_element_link_filtered(v_parser, _v_appsink, parser_filter_caps.get());

    raii_ptr<GstPad> v_depay_sink_pad(
        gst_element_get_static_pad(v_depay, "sink"),
        [](GstPad* pad){gst_object_unref(pad);}
    );

    GstPadLinkReturn ret = gst_pad_link(new_pad, v_depay_sink_pad.get());

    if(GST_PAD_LINK_FAILED(ret))
        R_LOG_ERROR("VIDEO PAD LINK FAILED");

    g_signal_connect(_v_appsink, "new-sample", G_CALLBACK(_new_video_sample), this);
}

void r_gst_source::_attach_h265_video_pipeline(GstPad* new_pad)
{
    GstElement* v_depay = gst_element_factory_make("rtph265depay", (_name_prefix + "rtph265depay").c_str());

    GstElement* v_parser = gst_element_factory_make("h265parse", (_name_prefix + "h265parse").c_str());
    g_object_set(G_OBJECT(v_parser), "config-interval", -1, NULL);

    _v_appsink = gst_element_factory_make("appsink", (_name_prefix + "v_appsink").c_str());

    g_object_set(G_OBJECT(_v_appsink), "emit-signals", TRUE, "sync", FALSE, NULL);

    gst_bin_add_many(GST_BIN(_pipeline), v_depay, v_parser, _v_appsink, NULL);

    gst_element_sync_state_with_parent(v_depay);
    gst_element_sync_state_with_parent(v_parser);
    gst_element_sync_state_with_parent(_v_appsink);

    raii_ptr<GstCaps> depay_filter_caps(
        gst_caps_new_simple(
            "video/x-h265",
            "stream-format", G_TYPE_STRING, "hvc1",
            "alignment", G_TYPE_STRING, "au",
            NULL
        ),
        [](GstCaps* caps){gst_caps_unref(caps);}
    );

    auto linked = gst_element_link_filtered(v_depay, v_parser, depay_filter_caps.get());

    raii_ptr<GstCaps> parser_filter_caps(
        gst_caps_new_simple(
            "video/x-h265",
            "stream-format", G_TYPE_STRING, "byte-stream",
            "alignment", G_TYPE_STRING, "au",
            NULL
        ),
        [](GstCaps* caps){gst_caps_unref(caps);}
    );

    auto linked2 = gst_element_link_filtered(v_parser, _v_appsink, parser_filter_caps.get());

    raii_ptr<GstPad> v_depay_sink_pad(
        gst_element_get_static_pad(v_depay, "sink"),
        [](GstPad* pad){gst_object_unref(pad);}
    );

    GstPadLinkReturn ret = gst_pad_link(new_pad, v_depay_sink_pad.get());

    if(GST_PAD_LINK_FAILED(ret))
        R_LOG_ERROR("VIDEO PAD LINK FAILED");

    g_signal_connect(_v_appsink, "new-sample", G_CALLBACK(_new_video_sample), this);
}

void r_gst_source::_attach_aac_audio_pipeline(GstPad* new_pad, r_encoding encoding)
{
    GstElement* a_depay = (encoding==AAC_LATM_ENCODING)?gst_element_factory_make("rtpmp4adepay", (_name_prefix + "rtpmp4adepay").c_str()):gst_element_factory_make("rtpmp4gdepay", (_name_prefix + "rtpmp4gdepay").c_str());

    GstElement* a_parser = gst_element_factory_make("aacparse", (_name_prefix + "aacparse").c_str());

    _a_appsink = gst_element_factory_make("appsink", (_name_prefix + "a_appsink").c_str());
    g_object_set(G_OBJECT(_a_appsink), "emit-signals", TRUE, "sync", FALSE, NULL);

    gst_bin_add_many(GST_BIN(_pipeline), a_depay, a_parser, _a_appsink, NULL);

    gst_element_sync_state_with_parent(a_depay);
    gst_element_sync_state_with_parent(a_parser);
    gst_element_sync_state_with_parent(_a_appsink);

    auto linked = gst_element_link_many(a_depay, a_parser, _a_appsink, NULL);

    raii_ptr<GstPad> a_depay_sink_pad(
        gst_element_get_static_pad(a_depay, "sink"),
        [](GstPad* pad){gst_object_unref(pad);}
    );

    GstPadLinkReturn ret = gst_pad_link(new_pad, a_depay_sink_pad.get());

    if(GST_PAD_LINK_FAILED(ret))
        R_LOG_ERROR("AUDIO PAD LINK FAILED");

    g_signal_connect(_a_appsink, "new-sample", G_CALLBACK(_new_audio_sample), this);
}

void r_gst_source::_attach_mulaw_audio_pipeline(GstPad* new_pad)
{
    GstElement* a_depay = gst_element_factory_make("rtppcmudepay", (_name_prefix + "rtppcmudepay").c_str());

    _a_appsink = gst_element_factory_make("appsink", (_name_prefix + "a_appsink").c_str());
    g_object_set(G_OBJECT(_a_appsink), "emit-signals", TRUE, "sync", FALSE, NULL);

    gst_bin_add_many(GST_BIN(_pipeline), a_depay, _a_appsink, NULL);

    gst_element_sync_state_with_parent(a_depay);
    gst_element_sync_state_with_parent(_a_appsink);

    auto linked = gst_element_link_many(a_depay, _a_appsink, NULL);

    raii_ptr<GstPad> a_depay_sink_pad(
        gst_element_get_static_pad(a_depay, "sink"),
        [](GstPad* pad){gst_object_unref(pad);}
    );

    GstPadLinkReturn ret = gst_pad_link(new_pad, a_depay_sink_pad.get());

    if(GST_PAD_LINK_FAILED(ret))
        R_LOG_ERROR("AUDIO PAD LINK FAILED");

    g_signal_connect(_a_appsink, "new-sample", G_CALLBACK(_new_audio_sample), this);
}

void r_gst_source::_attach_alaw_audio_pipeline(GstPad* new_pad)
{
    GstElement* a_depay = gst_element_factory_make("rtppcadepay", (_name_prefix + "rtppcadepay").c_str());

    _a_appsink = gst_element_factory_make("appsink", (_name_prefix + "a_appsink").c_str());
    g_object_set(G_OBJECT(_a_appsink), "emit-signals", TRUE, "sync", FALSE, NULL);

    gst_bin_add_many(GST_BIN(_pipeline), a_depay, _a_appsink, NULL);

    gst_element_sync_state_with_parent(a_depay);
    gst_element_sync_state_with_parent(_a_appsink);

    auto linked = gst_element_link_many(a_depay, _a_appsink, NULL);

    raii_ptr<GstPad> a_depay_sink_pad(
        gst_element_get_static_pad(a_depay, "sink"),
        [](GstPad* pad){gst_object_unref(pad);}
    );

    GstPadLinkReturn ret = gst_pad_link(new_pad, a_depay_sink_pad.get());

    if(GST_PAD_LINK_FAILED(ret))
        R_LOG_ERROR("AUDIO PAD LINK FAILED");

    g_signal_connect(_a_appsink, "new-sample", G_CALLBACK(_new_audio_sample), this);
}

void r_gst_source::_attach_g726_audio_pipeline(GstPad* new_pad)
{
    GstElement* a_depay = gst_element_factory_make("rtpg726depay", (_name_prefix + "rtpg726depay").c_str());

    _a_appsink = gst_element_factory_make("appsink", (_name_prefix + "a_appsink").c_str());
    g_object_set(G_OBJECT(_a_appsink), "emit-signals", TRUE, "sync", FALSE, NULL);

    gst_bin_add_many(GST_BIN(_pipeline), a_depay, _a_appsink, NULL);

    gst_element_sync_state_with_parent(a_depay);
    gst_element_sync_state_with_parent(_a_appsink);

    auto linked = gst_element_link_many(a_depay, _a_appsink, NULL);

    raii_ptr<GstPad> a_depay_sink_pad(
        gst_element_get_static_pad(a_depay, "sink"),
        [](GstPad* pad){gst_object_unref(pad);}
    );

    GstPadLinkReturn ret = gst_pad_link(new_pad, a_depay_sink_pad.get());

    if(GST_PAD_LINK_FAILED(ret))
        R_LOG_ERROR("AUDIO PAD LINK FAILED");

    g_signal_connect(_a_appsink, "new-sample", G_CALLBACK(_new_audio_sample), this);
}

GstFlowReturn r_gst_source::_new_video_sample(GstElement* elt, r_gst_source* src)
{
    lock_guard<mutex> g(src->_sample_cb_lock);

    raii_ptr<GstSample> sample(
        gst_app_sink_pull_sample(GST_APP_SINK(elt)),
        [](GstSample* sample){gst_sample_unref(sample);}
    );

    GstSegment* seg = gst_sample_get_segment(sample.get());

    auto result = GST_FLOW_OK;

    try
    {
        r_gst_buffer buffer(gst_sample_get_buffer(sample.get()));

        auto info = buffer.map(r_gst_buffer::MT_READ);

        uint32_t ft = (src->_h264_nal_parser)?src->_parse_h264(src->_h264_nal_parser, info.data(), info.size()):src->_parse_h265(src->_h265_nal_parser, info.data(), info.size());

        bool is_picture = (src->_h264_nal_parser)?src->_is_h264_picture(ft):src->_is_h265_picture(ft);

        bool key = is_picture && !GST_BUFFER_FLAG_IS_SET(buffer.get(), GST_BUFFER_FLAG_DELTA_UNIT);

        auto sample_pts = GST_BUFFER_PTS(buffer.get());
        bool has_pts = (sample_pts != GST_CLOCK_TIME_NONE);
        auto sample_dts = GST_BUFFER_DTS(buffer.get());
        bool has_dts = (sample_dts != GST_CLOCK_TIME_NONE);

        // OK, this is kind of a workaround for a specific axis camera. Basically, we see an
        // access unit containing an SEI but with a valid timestamp and then immediately after
        // we see an access unit with an IDR but with an invalid timestamp. So, we're detecting
        // that here and buffering the SEI timestamp... which we will re-use for the IDR. Its clear
        // to me that the intention of the camera was to have the SEI be in the same access unit as
        // the IDR, but gstreamer is not doing that.
        src->_sei_ts_hack(buffer.get(), has_pts, is_picture, sample_pts);

        // first, save gstreamers time info into the sample context...
        gst_time_info ti;
        ti.pts = sample_pts;
        ti.pts_running_time = gst_segment_to_running_time(seg, GST_FORMAT_TIME, ti.pts) / 1000000;
        ti.dts = sample_dts;
        ti.dts_running_time = gst_segment_to_running_time(seg, GST_FORMAT_TIME, ti.dts) / 1000000;

        src->_sample_context._gst_time_info = ti;

        // now, do our pts work...
        int64_t pts = 0;
        if(has_pts)
            pts = (int64_t)GST_TIME_AS_MSECONDS(sample_pts);
        else if(src->_last_v_pts_valid)
            pts = src->_last_v_pts + 1;

        if(!src->_sample_sent)
            src->_sample_context._stream_start_time = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        int64_t last_pts = (src->_last_v_pts_valid)?src->_last_v_pts:0;

        if(!src->_video_sample_cb.is_null() && std::llabs(pts - last_pts) < MSECS_IN_TEN_MINUTES) // 10 minutes
        {
            src->_video_sample_cb.value()(src->_sample_context, buffer, key, pts);
            src->_sample_sent = true;
            src->_video_sample_sent = true;

            src->_last_v_pts = pts;
            src->_last_v_pts_valid = true;
        }
    }
    catch(const r_exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
        result = GST_FLOW_ERROR;
    }

    return result;
}

GstFlowReturn r_gst_source::_new_audio_sample(GstElement* elt, r_gst_source* src)
{
    lock_guard<mutex> g(src->_sample_cb_lock);

    raii_ptr<GstSample> sample(
        gst_app_sink_pull_sample(GST_APP_SINK(elt)),
        [](GstSample* sample){gst_sample_unref(sample);}
    );

    GstSegment* seg = gst_sample_get_segment(sample.get());

    auto result = GST_FLOW_OK;

    try
    {
        r_gst_buffer buffer(gst_sample_get_buffer(sample.get()));

        auto info = buffer.map(r_gst_buffer::MT_READ);

        auto sample_pts = GST_BUFFER_PTS(buffer.get());
        bool sample_dts = GST_BUFFER_DTS(buffer.get());

        if(sample_pts != GST_CLOCK_TIME_NONE)
        {
            // first, save gstreamers time info into the sample context...
            gst_time_info ti;
            ti.pts = sample_pts;
            ti.pts_running_time = gst_segment_to_running_time(seg, GST_FORMAT_TIME, ti.pts) / 1000000;
            ti.dts = sample_dts;
            ti.dts_running_time = gst_segment_to_running_time(seg, GST_FORMAT_TIME, ti.dts) / 1000000;

            src->_sample_context._gst_time_info = ti;;

            // now, do our pts work...
            auto pts = (int64_t)GST_TIME_AS_MSECONDS(sample_pts);

            if(!src->_sample_sent)
            {
                src->_sample_context._stream_start_time = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            }

            if(!src->_audio_sample_sent)
                src->_parse_audio_sink_caps();

            int64_t last_pts = (src->_last_a_pts_valid)?src->_last_a_pts:0;

            if(!src->_audio_sample_cb.is_null() && std::llabs(pts - last_pts) < MSECS_IN_TEN_MINUTES)
            {
                src->_audio_sample_cb.value()(src->_sample_context, buffer, true, pts);
                src->_sample_sent = true;
                src->_audio_sample_sent = true;

                src->_last_a_pts = pts;
                src->_last_a_pts_valid = true;
            }
        }
    }
    catch(const r_exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
        result = GST_FLOW_ERROR;
    }

    return result;
}

gboolean r_gst_source::_bus_callbackS(GstBus* bus, GstMessage* message, gpointer data)
{
    try
    {
        return ((r_gst_source*)data)->_bus_callback(bus, message);
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
        g_set_error(NULL, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, "bus callback error");
    }

    return FALSE;
}

static bool _is_pipeline_msg(GstElement* pipeline, GstMessage* msg)
{
    return GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline);
}

static bool _is_state_change_msg(GstMessage* msg)
{
    return msg->type == GST_MESSAGE_STATE_CHANGED;
}

gboolean r_gst_source::_bus_callback(GstBus* bus, GstMessage* message)
{
    if(_is_pipeline_msg(_pipeline, message) && _is_state_change_msg(message))
    {
        GstState from, to, pending;
        gst_message_parse_state_changed(message, &from, &to, &pending);

        if(to == GST_STATE_READY)
        {
            if(!_ready_cb.is_null())
                _ready_cb.value()();
        }
    }

    return TRUE;
}

void r_gst_source::_on_sdp_callbackS(GstElement* src, GstSDPMessage* sdp, gpointer data)
{
    try
    {
        ((r_gst_source*)data)->_on_sdp_callback(src, sdp);
    }
    catch(const std::exception& e)
    {
        R_LOG_ERROR("%s", e.what());
        g_set_error(NULL, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, "sdp callback error");
    }
}

void r_gst_source::_on_sdp_callback(GstElement* src, GstSDPMessage* sdp)
{
    // For details on each field in GstSDPMessage see:
    // https://gstreamer.freedesktop.org/documentation/sdp/gstsdpmessage.html?gi-language=c

    auto sdp_txt = gst_sdp_message_as_text(sdp);

    _sample_context._sdp_text = string(sdp_txt);

    g_free(sdp_txt);

    for(unsigned int i = 0; i < sdp->medias->len; ++i)
    {
        auto m = &g_array_index(sdp->medias, GstSDPMedia, i);

        r_sdp_media media;

        auto media_str = r_string_utils::to_lower(string(m->media));

        if(media_str == "video")
            media.type = VIDEO_MEDIA;
        else if(media_str == "audio")
            media.type = AUDIO_MEDIA;
        else continue;

        // Note: If its a GArray of object pointers you use & in front of g_array_index

        for(unsigned int ii = 0; ii < m->fmts->len; ++ii)
        {
            // Note: for a string (gchar*) dont put prefix & on g_array_index
            media.formats.push_back(stoi(string(g_array_index(m->fmts, gchar*, ii))));
        }

        for(unsigned int ii = 0; ii < m->attributes->len; ++ii)
        {
            auto attr = &g_array_index(m->attributes, GstSDPAttribute, ii);

            string attr_key(attr->key);
            string attr_val(attr->value);

            // Note: we might want to parse fmtp lines here as well. fmtp attributes occur for each
            // available video stream and are slightly different for h.264
            // fmtp==96 packetization-mode=1;profile-level-id=64000a;sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA
            // and h.265
            // fmtp==96 sprop-vps=QAEMAf//AWAAAAMAkAAAAwAAAwAelZgJ;sprop-sps=QgEBAWAAAAMAkAAAAwAAAwAeoDCDn1llZrkyvAWoCAgIIAAAfSAAC7gB;sprop-pps=RAHBc9GJ

            if(attr_key == "rtpmap")
            {
                // a=rtpmap:96 H264/90000

                auto outer_parts = r_string_utils::split(attr_val, " ");
                auto inner_parts = r_string_utils::split(outer_parts[1], "/");

                r_rtp_map rtpmap;
                rtpmap.encoding = str_to_encoding(inner_parts[0]);
                rtpmap.time_base = stoi(inner_parts[1]);
                media.rtpmaps.insert(make_pair(stoi(outer_parts[0]), rtpmap));
            }
            else if(attr_key == "fmtp")
            {
                auto outer_parts = r_string_utils::split(attr_val, " ");
                auto inner_parts = r_string_utils::split(outer_parts[1], ";");
                for(auto nvp : inner_parts)
                {
                    auto parts = r_string_utils::split(nvp, "=");
                    if(parts.size() == 2)
                        media.attributes.insert(make_pair(r_string_utils::strip(parts[0]), r_string_utils::strip(parts[1])));
                }
            }
            else media.attributes.insert(make_pair(r_string_utils::strip(attr_key), r_string_utils::strip(attr_val)));
        }

        _sample_context._sdp_medias.insert(make_pair(string(m->media), media));
    }

    if(!_sdp_media_cb.is_null())
        _sdp_media_cb.value()(_sample_context._sdp_medias);
}

uint32_t r_gst_source::_parse_h264(GstH264NalParser* parser, const uint8_t* p, size_t size)
{
    uint32_t elements = 0;

    size_t pos = 0;

    GstH264NalUnit nal_unit;
    while(pos < size)
    {
        gst_h264_parser_identify_nalu(parser, p, (guint)pos, (guint)size, &nal_unit);

        if(nal_unit.type == GST_H264_NAL_UNKNOWN)
            elements |= (uint32_t)H264_NT::UNKNOWN;
        else if(nal_unit.type == GST_H264_NAL_SLICE)
            elements |= (uint32_t)H264_NT::SLICE;
        else if(nal_unit.type == GST_H264_NAL_SLICE_DPA)
            elements |= (uint32_t)H264_NT::SLICE_DPA;
        else if(nal_unit.type == GST_H264_NAL_SLICE_DPB)
            elements |= (uint32_t)H264_NT::SLICE_DPB;
        else if(nal_unit.type == GST_H264_NAL_SLICE_DPC)
            elements |= (uint32_t)H264_NT::SLICE_DPC;
        else if(nal_unit.type == GST_H264_NAL_SLICE_IDR)
            elements |= (uint32_t)H264_NT::SLICE_IDR;
        else if(nal_unit.type == GST_H264_NAL_SEI)
            elements |= (uint32_t)H264_NT::SEI;
        else if(nal_unit.type == GST_H264_NAL_SPS)
            elements |= (uint32_t)H264_NT::SPS;
        else if(nal_unit.type == GST_H264_NAL_PPS)
            elements |= (uint32_t)H264_NT::PPS;
        else if(nal_unit.type == GST_H264_NAL_AU_DELIMITER)
            elements |= (uint32_t)H264_NT::AU_DELIMITER;
        else if(nal_unit.type == GST_H264_NAL_SEQ_END)
            elements |= (uint32_t)H264_NT::SEQ_END;
        else if(nal_unit.type == GST_H264_NAL_STREAM_END)
            elements |= (uint32_t)H264_NT::STREAM_END;
        else if(nal_unit.type == GST_H264_NAL_FILLER_DATA)
            elements |= (uint32_t)H264_NT::FILLER_DATA;
        else if(nal_unit.type == GST_H264_NAL_SPS_EXT)
            elements |= (uint32_t)H264_NT::SPS_EXT;
        else if(nal_unit.type == GST_H264_NAL_PREFIX_UNIT)
            elements |= (uint32_t)H264_NT::PREFIX_UNIT;
        else if(nal_unit.type == GST_H264_NAL_SUBSET_SPS)
            elements |= (uint32_t)H264_NT::SUBSET_SPS;
        else if(nal_unit.type == GST_H264_NAL_DEPTH_SPS)
            elements |= (uint32_t)H264_NT::DEPTH_SPS;
        else if(nal_unit.type == GST_H264_NAL_SLICE_AUX)
            elements |= (uint32_t)H264_NT::SLICE_AUX;
        else if(nal_unit.type == GST_H264_NAL_SLICE_EXT)
            elements |= (uint32_t)H264_NT::SLICE_EXT;
        else if(nal_unit.type == GST_H264_NAL_SLICE_DEPTH)
            elements |= (uint32_t)H264_NT::SLICE_DEPTH;

        gst_h264_parser_parse_nal(parser, &nal_unit);

        pos = nal_unit.offset + nal_unit.size;
    }

    return elements;
}

uint32_t r_gst_source::_parse_h265(GstH265Parser* parser, const uint8_t* p, size_t size)
{
    uint32_t elements = 0;
    size_t pos = 0;

    GstH265NalUnit nal_unit;
    while(pos < size)
    {
        gst_h265_parser_identify_nalu(parser, p, (guint)pos, (guint)size, &nal_unit);

        if(nal_unit.type == GST_H265_NAL_SLICE_TRAIL_N)
            elements |= (uint32_t)H265_NT::SLICE_TRAIL_N;
        else if(nal_unit.type == GST_H265_NAL_SLICE_TRAIL_R)
            elements |= (uint32_t)H265_NT::SLICE_TRAIL_R;
        else if(nal_unit.type == GST_H265_NAL_SLICE_TSA_N)
            elements |= (uint32_t)H265_NT::SLICE_TSA_N;
        else if(nal_unit.type == GST_H265_NAL_SLICE_TSA_R)
            elements |= (uint32_t)H265_NT::SLICE_TSA_R;
        else if(nal_unit.type == GST_H265_NAL_SLICE_STSA_N)
            elements |= (uint32_t)H265_NT::SLICE_STSA_N;
        else if(nal_unit.type == GST_H265_NAL_SLICE_STSA_R)
            elements |= (uint32_t)H265_NT::SLICE_STSA_R;
        else if(nal_unit.type == GST_H265_NAL_SLICE_RADL_N)
            elements |= (uint32_t)H265_NT::SLICE_RADL_N;
        else if(nal_unit.type == GST_H265_NAL_SLICE_RADL_R)
            elements |= (uint32_t)H265_NT::SLICE_RADL_R;
        else if(nal_unit.type == GST_H265_NAL_SLICE_RASL_N)
            elements |= (uint32_t)H265_NT::SLICE_RASL_N;
        else if(nal_unit.type == GST_H265_NAL_SLICE_RASL_R)
            elements |= (uint32_t)H265_NT::SLICE_RASL_R;
        else if(nal_unit.type == GST_H265_NAL_SLICE_BLA_W_LP)
            elements |= (uint32_t)H265_NT::SLICE_BLA_W_LP;
        else if(nal_unit.type == GST_H265_NAL_SLICE_BLA_W_RADL)
            elements |= (uint32_t)H265_NT::SLICE_BLA_W_RADL;
        else if(nal_unit.type == GST_H265_NAL_SLICE_BLA_N_LP)
            elements |= (uint32_t)H265_NT::SLICE_BLA_N_LP;
        else if(nal_unit.type == GST_H265_NAL_SLICE_IDR_W_RADL)
            elements |= (uint32_t)H265_NT::SLICE_IDR_W_RADL;
        else if(nal_unit.type == GST_H265_NAL_SLICE_IDR_N_LP)
            elements |= (uint32_t)H265_NT::SLICE_IDR_N_LP;
        else if(nal_unit.type == GST_H265_NAL_SLICE_CRA_NUT)
            elements |= (uint32_t)H265_NT::SLICE_CRA_NUT;
        else if(nal_unit.type == GST_H265_NAL_VPS)
            elements |= (uint32_t)H265_NT::VPS;
        else if(nal_unit.type == GST_H265_NAL_SPS)
            elements |= (uint32_t)H265_NT::SPS;
        else if(nal_unit.type == GST_H265_NAL_PPS)
            elements |= (uint32_t)H265_NT::PPS;
        else if(nal_unit.type == GST_H265_NAL_AUD)
            elements |= (uint32_t)H265_NT::AUD;
        else if(nal_unit.type == GST_H265_NAL_EOS)
            elements |= (uint32_t)H265_NT::EOS;
        else if(nal_unit.type == GST_H265_NAL_EOB)
            elements |= (uint32_t)H265_NT::EOB;
        else if(nal_unit.type == GST_H265_NAL_FD)
            elements |= (uint32_t)H265_NT::FD;
        else if(nal_unit.type == GST_H265_NAL_PREFIX_SEI)
            elements |= (uint32_t)H265_NT::PREFIX_SEI;
        else if(nal_unit.type == GST_H265_NAL_SUFFIX_SEI)
            elements |= (uint32_t)H265_NT::SUFFIX_SEI;

        gst_h265_parser_parse_nal(parser, &nal_unit);

        pos = nal_unit.offset + nal_unit.size;
    }

    return elements;
}

bool r_gst_source::_is_h264_picture(uint32_t ft)
{
    return (ft & (uint32_t)H264_NT::SLICE) ||
           (ft & (uint32_t)H264_NT::SLICE_DPA) ||
           (ft & (uint32_t)H264_NT::SLICE_DPB) ||
           (ft & (uint32_t)H264_NT::SLICE_DPC) ||
           (ft & (uint32_t)H264_NT::SLICE_IDR);
}

bool r_gst_source::_is_h265_picture(uint32_t ft)
{
    return (ft & (uint32_t)H265_NT::SLICE_TRAIL_N) ||
           (ft & (uint32_t)H265_NT::SLICE_TRAIL_R) ||
           (ft & (uint32_t)H265_NT::SLICE_TSA_N) ||
           (ft & (uint32_t)H265_NT::SLICE_TSA_R) ||
           (ft & (uint32_t)H265_NT::SLICE_STSA_N) ||
           (ft & (uint32_t)H265_NT::SLICE_STSA_R) ||
           (ft & (uint32_t)H265_NT::SLICE_RADL_N) ||
           (ft & (uint32_t)H265_NT::SLICE_RADL_R) ||
           (ft & (uint32_t)H265_NT::SLICE_RASL_N) ||
           (ft & (uint32_t)H265_NT::SLICE_RASL_R) ||
           (ft & (uint32_t)H265_NT::SLICE_BLA_W_LP) ||
           (ft & (uint32_t)H265_NT::SLICE_BLA_W_RADL) ||
           (ft & (uint32_t)H265_NT::SLICE_BLA_N_LP) ||
           (ft & (uint32_t)H265_NT::SLICE_IDR_W_RADL) ||
           (ft & (uint32_t)H265_NT::SLICE_IDR_N_LP) ||
           (ft & (uint32_t)H265_NT::SLICE_CRA_NUT);
}

void r_gst_source::_parse_audio_sink_caps()
{
    raii_ptr<GstPad> sinkpad(
        gst_element_get_static_pad(_a_appsink, "sink"),
        [](GstPad* pad){gst_object_unref(pad);}
    );

    raii_ptr<GstCaps> caps(
        gst_pad_get_current_caps(sinkpad.get()),
        [](GstCaps* caps){gst_caps_unref(caps);}
    );

    GstStructure* structure = gst_caps_get_structure(caps.get(), 0);
    if(!structure)
        R_THROW(("Unable to query audio appsink sink pad caps."));

    int channels = 0;
    if(gst_structure_get_int(structure, "channels", &channels) == TRUE)
        _sample_context._audio_channels.set_value((uint8_t)channels);

    int rate = 0;
    if(gst_structure_get_int(structure, "rate", &rate) == TRUE)
        _sample_context._audio_sample_rate.set_value((uint32_t)rate);
}

void r_gst_source::_sei_ts_hack(GstBuffer* buffer, bool& has_pts, bool is_picture, uint64_t& sample_pts)
{
    // OK, this is kind of a workaround for a specific axis camera. Basically, we see an
    // access unit containing an SEI but with a valid timestamp and then immediately after
    // we see an access unit with an IDR but with an invalid timestamp. So, we're detecting
    // that here and buffering the SEI timestamp... which we will re-use for the IDR. Seems
    // clear to me that the intent of the camera was for the SEI to precede the IDR and be
    // included with it... but they are never the less putting it in a separate access unit!

    // Note: This is caused by using alignment="au" on our parsers. "au" seems to mostly be what
    // we want but if we had used alignment="nal" WE would be determining where the frame boundaries are.
    // We could have then just included the SEI NAL in subsequent IDR.

    // First, we only want to use the buffered timestamp on the frame immediately after so if
    // we have a PTS but we still have one buffered clear our flag so we never use it.
    if(has_pts && _buffered_ts)
        _buffered_ts = false;

    // If it's not a picture but it has a PTS then grab the PTS and set the flag.
    if(!is_picture && has_pts)
    {
        _buffered_ts = true;
        _buffered_ts_value = GST_BUFFER_PTS(buffer);
    }

    // If it is a picture and it doesn't have a pts AND we have one buffered use it and
    // then clear the flag.
    if(is_picture && !has_pts && _buffered_ts)
    {
        _buffered_ts = false;

        GST_BUFFER_PTS(buffer) = _buffered_ts_value;

        has_pts = true;
        sample_pts = GST_BUFFER_PTS(buffer);
    }
}

void r_gst_source::_clear() noexcept
{
    if(_h264_nal_parser)
    {
        gst_h264_nal_parser_free(_h264_nal_parser);
        _h264_nal_parser = nullptr;
    }

    if(_h265_nal_parser)
    {
        gst_h265_parser_free(_h265_nal_parser);
        _h265_nal_parser = nullptr;
    }

    if(_pipeline)
    { 
        g_source_remove(_bus_watch_id);
        _bus_watch_id = 0;

        stop();

        gst_object_unref(_pipeline);
        _pipeline = nullptr;
    };
}
