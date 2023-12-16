
#ifdef IS_WINDOWS
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif
#ifdef IS_LINUX
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <gst/rtp/gstrtpbuffer.h>
#ifdef IS_LINUX
#pragma GCC diagnostic pop
#endif
#ifdef IS_WINDOWS
#pragma warning( pop )
#endif

#include "r_vss/r_recording_context.h"
#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_ws.h"
#include "r_pipeline/r_arg.h"
#include "r_pipeline/r_stream_info.h"
#include "r_mux/r_muxer.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_blob_tree.h"
#include <vector>

using namespace r_vss;
using namespace r_disco;
using namespace r_pipeline;
using namespace r_storage;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

r_recording_context::r_recording_context(r_stream_keeper* sk, const r_camera& camera, const string& top_dir, r_ws& ws) :
    _sk(sk),
    _camera(camera),
    _top_dir(top_dir),
    _source(camera.friendly_name + "_"),
    _storage_file(top_dir + PATH_SLASH + "video" + PATH_SLASH + camera.record_file_path.value()),
    _maybe_storage_write_context(),
    _last_v_time(system_clock::now()),
    _last_a_time(system_clock::now()),
    _has_audio(false),
    _stream_start_ts(system_clock::now()),
    _v_bytes_received(0),
    _a_bytes_received(0),
    _sdp_medias(),
    _video_caps(),
    _audio_caps(),
    _restream_mount_path(),
    _live_restreaming_states_lok(),
    _live_restreaming_states(),
    _playback_restreaming_states_lok(),
    _playback_restreaming_states(),
    _got_first_audio_sample(false),
    _got_first_video_sample(false),
    _die(false),
    _ws(ws)
{
    vector<r_arg> arguments;
    add_argument(arguments, "url", _camera.rtsp_url.value());

    if(!_camera.rtsp_username.is_null())
        add_argument(arguments, "username", _camera.rtsp_username.value());
    if(!_camera.rtsp_password.is_null())
        add_argument(arguments, "password", _camera.rtsp_password.value());
    
    _source.set_args(arguments);

    _source.set_audio_sample_cb([this](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts){

        try
        {  
            if(_maybe_storage_write_context.is_null())
                _maybe_storage_write_context = _create_storage_writer_context();

            auto ctx = _maybe_storage_write_context.value();
            if(ctx.audio_codec_parameters.find("encoded_audio_sample_caps") == std::string::npos)
            {
                auto acaps = _source.get_audio_caps();
                gchar* acaps_sp = gst_caps_serialize(acaps.value(), GST_SERIALIZE_FLAG_NONE);
                string acaps_s(acaps_sp);
                auto encoded_acaps_s = r_string_utils::to_base64(acaps_s.data(), acaps_s.size());
                ctx.audio_codec_parameters += ", encoded_audio_sample_caps=" + encoded_acaps_s;
                _maybe_storage_write_context.set_value(ctx);
            }

            if(!this->_got_first_audio_sample)
            {
                this->_got_first_audio_sample = true;
                _final_storage_writer_audio_config(sc);
            }

            _last_a_time = system_clock::now();
            auto mi = buffer.map(r_gst_buffer::MT_READ);
            _a_bytes_received += mi.size();

            if(this->_audio_caps.is_null())
                this->_audio_caps = _source.get_audio_caps();

            if(!this->_video_caps.is_null() && this->_restream_mount_path.empty())
                this->_restream_mount_path = this->_sk->add_restream_mount(_sdp_medias, _camera, this, sc.video_encoding(), sc.audio_encoding());

            auto ts = sc.stream_start_ts() + pts;
            this->_storage_file.write_frame(
                this->_maybe_storage_write_context.value(),
                R_STORAGE_MEDIA_TYPE_AUDIO,
                mi.data(),
                mi.size(),
                key,
                ts,
                pts
            );

            lock_guard<mutex> g(this->_live_restreaming_states_lok);

            for(auto& lrs : this->_live_restreaming_states)
            {
                if(lrs.second->live_restream_key_sent)
                {
                    if(!lrs.second->first_restream_a_times_set)
                    {
                        lrs.second->first_restream_a_times_set = true;
                        lrs.second->first_restream_a_pts = pts * 1000000;
                        lrs.second->first_restream_a_dts = pts * 1000000;
                    }

                    _frame_context fc;
                    fc.gst_pts = (pts * 1000000) - lrs.second->first_restream_a_pts;
                    fc.gst_dts = (pts * 1000000) - lrs.second->first_restream_a_dts;

                    fc.key = key;
                    fc.buffer = buffer;
                    lrs.second->audio_samples.post(fc);
                }
            }
        }
        catch(exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
            _die = true;
        }
    });

    _source.set_video_sample_cb([this](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts){

        try
        {
            if(_maybe_storage_write_context.is_null())
                _maybe_storage_write_context = _create_storage_writer_context();

            auto ctx = _maybe_storage_write_context.value();
            if(ctx.video_codec_parameters.find("encoded_video_sample_caps") == std::string::npos)
            {
                auto vcaps = _source.get_video_caps();
                gchar* vcaps_sp = gst_caps_serialize(vcaps.value(), GST_SERIALIZE_FLAG_NONE);
                string vcaps_s(vcaps_sp);
                auto encoded_vcaps_s = r_string_utils::to_base64(vcaps_s.data(), vcaps_s.size());
                ctx.video_codec_parameters += ", encoded_video_sample_caps=" + encoded_vcaps_s;
                _maybe_storage_write_context.set_value(ctx);
            }

            if(!this->_got_first_video_sample)
            {
                this->_got_first_video_sample = true;
                _final_storage_writer_video_config(sc);
            }

            _last_v_time = system_clock::now();
            auto mi = buffer.map(r_gst_buffer::MT_READ);
            _v_bytes_received += mi.size();
            if(this->_video_caps.is_null())
            {
                this->_video_caps = _source.get_video_caps();
                if(!this->_has_audio)
                    this->_restream_mount_path = this->_sk->add_restream_mount(_sdp_medias, _camera, this, sc.video_encoding(), sc.audio_encoding());
            }

            auto ts = sc.stream_start_ts() + pts;
            this->_storage_file.write_frame(
                this->_maybe_storage_write_context.value(),
                R_STORAGE_MEDIA_TYPE_VIDEO,
                mi.data(),
                mi.size(),
                key,
                ts,
                pts
            );

            bool do_motion = (!this->_camera.do_motion_detection.is_null())?this->_camera.do_motion_detection.value():false;

            if(do_motion && (key || GST_BUFFER_FLAG_IS_SET(buffer.get(), GST_BUFFER_FLAG_NON_DROPPABLE)))
            {
                this->_sk->post_key_frame_to_motion_engine(
                    buffer,
                    ts,
                    this->_maybe_storage_write_context.value().video_codec_name,
                    this->_maybe_storage_write_context.value().video_codec_parameters,
                    this->_camera.id
                );
            }

            lock_guard<mutex> g(this->_live_restreaming_states_lok);

            for(auto& lrs : this->_live_restreaming_states)
            {
                if(lrs.second->live_restream_key_sent || key)
                {
                    lrs.second->live_restream_key_sent = true;

                    if(!lrs.second->first_restream_v_times_set)
                    {
                        lrs.second->first_restream_v_times_set = true;
                        lrs.second->first_restream_v_pts = pts * 1000000;
                        lrs.second->first_restream_v_dts = pts * 1000000;
                    }

                    _frame_context fc;
                    fc.gst_pts = (pts*1000000) - lrs.second->first_restream_v_pts;
                    fc.gst_dts = (pts*1000000) - lrs.second->first_restream_v_dts;
                    fc.key = key;
                    fc.buffer = buffer;
                    lrs.second->video_samples.post(fc);
                }
            }
        }
        catch(exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
            _die = true;
        }
    });

    _source.set_sdp_media_cb([this](const map<string, r_sdp_media>& sdp_medias){
        this->_sdp_medias = sdp_medias;
        if(this->_sdp_medias.find("audio") != this->_sdp_medias.end())
            this->_has_audio = true;
    });

    //R_LOG_INFO("recording: camera.id=%s, file=%s, rtsp_url=%s", _camera.id.c_str(), _camera.record_file_path.value().c_str(), _camera.rtsp_url.value().c_str());
    printf("recording: camera.id=%s, file=%s, rtsp_url=%s\n", _camera.id.c_str(), _camera.record_file_path.value().c_str(), _camera.rtsp_url.value().c_str());
    fflush(stdout);

    _source.play();
}

r_recording_context::~r_recording_context() noexcept
{
    _sk->remove_restream_mount(_restream_mount_path);
    stop();
}

bool r_recording_context::dead() const
{
    auto now = system_clock::now();
    auto video_dead = ((now - _last_v_time) > seconds(20));
    bool is_dead = (_has_audio)?((now - _last_a_time) > seconds(20))||video_dead:video_dead;
    if(_die)
        is_dead = true;

    if(is_dead)
    {
        R_LOG_INFO("found dead stream: camera.id=%s", _camera.id.c_str());
        printf("found dead stream: camera.id=%s\n", _camera.id.c_str());
        fflush(stdout);
    }

    return is_dead;
}

r_camera r_recording_context::camera() const
{
    return _camera;
}

int32_t r_recording_context::bytes_per_second() const
{
    uint64_t div = duration_cast<seconds>(system_clock::now() - _stream_start_ts).count();
    if(div == 0)
        div = 1;
    return (int32_t)((_v_bytes_received + _a_bytes_received) / div);
}

static void _need_live_data_cbs(GstElement* appsrc, guint unused, live_restreaming_state* lrs)
{
    if(appsrc == lrs->v_appsrc)
    {
        auto sample = lrs->video_samples.poll(chrono::milliseconds(3000));
        if(!sample.is_null())
        {
            auto output_buffer = r_gst_buffer(gst_buffer_copy(sample.value().buffer.get()));
            GST_BUFFER_PTS(output_buffer.get()) = sample.value().gst_pts;
            GST_BUFFER_DTS(output_buffer.get()) = sample.value().gst_dts;
            int ret;
            g_signal_emit_by_name(appsrc, "push-buffer", output_buffer.get(), &ret);
        }
    }
    else
    {
        auto sample = lrs->audio_samples.poll(chrono::milliseconds(3000));
        if(!sample.is_null())
        {
            auto output_buffer = r_gst_buffer(gst_buffer_copy(sample.value().buffer.get()));
            GST_BUFFER_PTS(output_buffer.get()) = sample.value().gst_pts;
            GST_BUFFER_DTS(output_buffer.get()) = sample.value().gst_dts;
            int ret;
            g_signal_emit_by_name(appsrc, "push-buffer", output_buffer.get(), &ret);
        }
    }
}

static void _seek_live_data_cbs(GstElement* appsrc, guint64 offset, live_restreaming_state* lrs)
{
}

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


void r_recording_context::live_restream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media)
{
    auto lrs = make_shared<live_restreaming_state>();
    lrs->rc = this;
    lrs->media = media;

    auto element = gst_rtsp_media_get_element(media);
    if(!element)
        R_THROW(("Failed to get element from media in restream media configure."));

    // pay0 is the video payloader
    // pay1 is the audio payloader which might now actually be present

    // attach media cleanup callback to unset _live_restreaming flag
    g_object_set_data_full(G_OBJECT(media), "rtsp-extra-data", lrs.get(), (GDestroyNotify)_live_restream_cleanup_cbs);

    lrs->v_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "videosrc");
    if(!lrs->v_appsrc)
        R_THROW(("Failed to get videosrc from element in restream media configure."));

    gst_util_set_object_arg(G_OBJECT(lrs->v_appsrc), "format", "time");

    g_object_set(G_OBJECT(lrs->v_appsrc), "caps", (GstCaps*)_video_caps.value(), NULL);

    print_caps((GstCaps*)_video_caps.value(), "video caps: ");

    gchar* caps_ser = gst_caps_serialize((GstCaps*)_video_caps.value(), GST_SERIALIZE_FLAG_NONE);
    printf("%s\n", caps_ser);
    g_free(caps_ser);

//    gst_util_set_object_arg(G_OBJECT(_v_appsrc), "stream-type", "seekable");

//    g_object_set(G_OBJECT(_v_appsrc), "duration", 10000000000, NULL);

    g_signal_connect(lrs->v_appsrc, "need-data", (GCallback)_need_live_data_cbs, lrs.get());

    g_signal_connect(lrs->v_appsrc, "seek-data", (GCallback)_seek_live_data_cbs, lrs.get());

    lrs->a_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "audiosrc");

    if(lrs->a_appsrc)
    {
        gst_util_set_object_arg(G_OBJECT(lrs->a_appsrc), "format", "time");

        g_object_set(G_OBJECT(lrs->a_appsrc), "caps", (GstCaps*)_audio_caps.value(), NULL);

        print_caps((GstCaps*)_audio_caps.value(), "audio caps: ");

        caps_ser = gst_caps_serialize((GstCaps*)_audio_caps.value(), GST_SERIALIZE_FLAG_NONE);
        printf("%s\n", caps_ser);
        g_free(caps_ser);


//        gst_util_set_object_arg(G_OBJECT(_a_appsrc), "stream-type", "seekable");

//        g_object_set(G_OBJECT(_v_appsrc), "duration", 10000000000, NULL);

        g_signal_connect(lrs->a_appsrc, "need-data", (GCallback)_need_live_data_cbs, lrs.get());

        g_signal_connect(lrs->a_appsrc, "seek-data", (GCallback)_seek_live_data_cbs, lrs.get());
    }
    else R_LOG_ERROR("no audio appsrc");

    _live_restreaming_states[media] = lrs;
}

void r_recording_context::stop()
{
    _source.stop();

    this->_storage_file.finalize(this->_maybe_storage_write_context.value());
}

void r_recording_context::create_playback_mount(GstRTSPServer* server, GstRTSPMountPoints* mounts, uint64_t start_ts, uint64_t end_ts)
{
    system_clock::time_point start_tp{milliseconds{start_ts}};
    system_clock::time_point end_tp{milliseconds{end_ts}};

    auto contents = _ws.get_contents(_camera.id, start_tp, end_tp);

    if(contents.segments.empty())
    {
        R_LOG_ERROR("no segments found for playback restream mount: camera.friendly_name=%s, start_ts=%s, end_ts=%s", _camera.friendly_name.value().c_str(), r_string_utils::uint64_to_s(start_ts).c_str(), r_string_utils::uint64_to_s(end_ts).c_str());
        return;
    }

    auto first_segment = contents.segments.front();

    auto video_buffer = _ws.get_video(_camera.id, first_segment.start, first_segment.start + seconds(5));

    uint32_t version = 0;
    auto bt = r_blob_tree::deserialize(video_buffer.data(), video_buffer.size(), version);

    if(!bt.has_key("has_audio"))
        R_THROW(("Blob tree missing audio indicator."));

    bool has_audio = (bt["has_audio"].get_string() == "true")?true:false;

    if(!bt.has_key("video_codec_name"))
        R_THROW(("Blob tree missing video codec name."));

    auto video_codec_name = bt["video_codec_name"].get_string();

    if(!bt.has_key("video_codec_parameters"))
        R_THROW(("Blob tree missing video codec parameters."));

    auto video_codec_parameters = bt["video_codec_parameters"].get_string();

    string audio_codec_name, audio_codec_parameters;
    if(has_audio)
    {
        if(!bt.has_key("audio_codec_name"))
            R_THROW(("Blob tree missing audio codec name but has audio!"));
        audio_codec_name = bt["audio_codec_name"].get_string();

        if(!bt.has_key("audio_codec_parameters"))
            R_THROW(("Blob tree missing audio codec parameters but has audio!"));
        audio_codec_parameters = bt["audio_codec_parameters"].get_string();
    }

    auto video_encoding = r_pipeline::str_to_encoding(video_codec_name);

    r_utils::r_nullable<r_pipeline::r_encoding> maybe_audio_encoding;
    if(has_audio)
        maybe_audio_encoding.set_value(r_pipeline::str_to_encoding(audio_codec_name));

    auto launch_str = _sk->create_restream_launch_string(
        video_encoding,
        r_pipeline::encoding_to_pt(video_encoding),
        maybe_audio_encoding, 
        (maybe_audio_encoding.is_null())?0:r_pipeline::encoding_to_pt(maybe_audio_encoding.value())
    );

    // Ok, now that we have a launch string lets build our factory...

    auto factory = gst_rtsp_media_factory_new();
    if(!factory)
        R_THROW(("Failed to create restream media factory!"));

    gst_rtsp_media_factory_set_launch(factory, launch_str.c_str());

    gst_rtsp_media_factory_set_shared(factory, FALSE);

    g_signal_connect(factory, "media-configure", (GCallback)_playback_restream_media_configure_cbs, this);

    auto mount_path = r_string_utils::format("/%s_%lu_%lu", _camera.friendly_name.value().c_str(), start_ts, end_ts);

    gst_rtsp_mount_points_add_factory(mounts, mount_path.c_str(), factory);
}

tuple<string, string, string> r_recording_context::_get_playback_url_parts()
{
    GstRTSPContext* context = gst_rtsp_context_get_current();

    auto klass = GST_RTSP_CLIENT_GET_CLASS(context->client);
    shared_ptr<gchar> path_p(klass->make_path_from_uri(context->client, context->uri), [](gchar* p){if(p) g_free(p);});
    string path(path_p.get());

    if(r_string_utils::starts_with(path, "/"))
        path = path.substr(1);

    auto parts = r_string_utils::split(path, '_');

    // playback urls look like this: the_porch_7389303093_3838949845
    if(parts.size() < 3)
        R_THROW(("Unable to parse playback url (%s)!", path.c_str()));

    auto end_time_s = parts.back();
    parts.pop_back();

    if(!r_string_utils::is_integer(end_time_s))
        R_THROW(("Unable to parse playback url (%s)!", path.c_str()));

    auto start_time_s = parts.back();
    parts.pop_back();

    if(!r_string_utils::is_integer(start_time_s))
        R_THROW(("Unable to parse playback url (%s)!", path.c_str()));

    string friendly_name = r_string_utils::join(parts, '_');

    return make_tuple(friendly_name, start_time_s, end_time_s);
}

void r_recording_context::_live_restream_cleanup_cbs(live_restreaming_state* lrs)
{
    lock_guard<mutex> g(lrs->rc->_live_restreaming_states_lok);

    auto rc = lrs->rc;
    auto media = lrs->media;

    rc->_live_restreaming_states.erase(media);
}

tuple<bool, string, string, string, string, r_encoding, r_nullable<r_encoding>>
_fetch_stream_info(shared_ptr<playback_restreaming_state> prs, r_ws& ws, const string& camera_id)
{
    if(prs->con.segments.empty())
        R_THROW(("No segments found for playback restream mount."));

    auto first_segment = prs->con.segments.front();

    auto video_buffer = ws.get_video(camera_id, first_segment.start, first_segment.start + seconds(5));

    uint32_t version = 0;
    auto bt = r_blob_tree::deserialize(video_buffer.data(), video_buffer.size(), version);

    if(!bt.has_key("has_audio"))
        R_THROW(("Blob tree missing audio indicator."));

    bool has_audio = (bt["has_audio"].get_string() == "true")?true:false;

    if(!bt.has_key("video_codec_name"))
        R_THROW(("Blob tree missing video codec name."));
    
    if(!bt.has_key("video_codec_parameters"))
        R_THROW(("Blob tree missing video codec parameters."));

    if(has_audio)
    {
        if(!bt.has_key("audio_codec_name"))
            R_THROW(("Blob tree missing audio codec name but has audio!"));

        if(!bt.has_key("audio_codec_parameters"))
            R_THROW(("Blob tree missing audio codec parameters but has audio!"));
    }

    r_nullable<r_encoding> maybe_audio_encoding;

    if(has_audio)
        maybe_audio_encoding.set_value(r_pipeline::str_to_encoding(bt["audio_codec_name"].get_string()));

    return make_tuple(
        has_audio,
        bt["video_codec_name"].get_string(),
        bt["video_codec_parameters"].get_string(),
        (has_audio)?bt["audio_codec_name"].get_string():string(),
        (has_audio)?bt["audio_codec_parameters"].get_string():string(),
        r_pipeline::str_to_encoding(bt["video_codec_name"].get_string()),
        maybe_audio_encoding
    );
}

tuple<contents, std::chrono::milliseconds> _fetch_contents(r_ws& ws, const string& camera_id, const string& start_time_s, const string& end_time_s)
{
    system_clock::time_point start_tp{milliseconds{r_string_utils::s_to_uint64(start_time_s)}};
    system_clock::time_point end_tp{milliseconds{r_string_utils::s_to_uint64(end_time_s)}};

    auto con = ws.get_contents(camera_id, start_tp, end_tp);

    if(con.segments.empty())
        R_THROW(("No segments found for playback restream mount."));

    milliseconds playback_duration{0};
    for(auto& s : con.segments)
        playback_duration += duration_cast<milliseconds>(s.end - s.start);

    return make_tuple(con, playback_duration);
}

void r_recording_context::_playback_restream_media_configure_cbs(GstRTSPMediaFactory* factory, GstRTSPMedia* media, r_recording_context* rc)
{
    rc->_playback_restream_media_configure(factory, media);
}

r_gst_caps _create_caps(const string& codec_parameters, const string& key)
{
    auto params = r_string_utils::split(codec_parameters, ", ");
    for(auto p : params)
    {
        auto kv = r_string_utils::split(p, "=");
        if(kv.size() == 2)
        {
            if(kv[0] == key)
            {
                auto decoded = r_string_utils::from_base64(kv[1]);
                string caps_s((char*)decoded.data(), decoded.size());
                return r_gst_caps(gst_caps_from_string(caps_s.c_str()));
            }
        }
    }

    R_THROW(("Unable to find %s in codec parameters!", key.c_str()));
}

static void _need_playback_data_cbs(GstElement* appsrc, guint unused, playback_restreaming_state* prs)
{
    R_LOG_ERROR("NEED PLAYBACK DATA");
#if 0
    if(appsrc == lrs->v_appsrc)
    {
        auto sample = lrs->video_samples.poll(chrono::milliseconds(3000));
        if(!sample.is_null())
        {
            auto output_buffer = r_gst_buffer(gst_buffer_copy(sample.value().buffer.get()));
            GST_BUFFER_PTS(output_buffer.get()) = sample.value().gst_pts;
            GST_BUFFER_DTS(output_buffer.get()) = sample.value().gst_dts;
            int ret;
            g_signal_emit_by_name(appsrc, "push-buffer", output_buffer.get(), &ret);
        }
    }
    else
    {
        auto sample = lrs->audio_samples.poll(chrono::milliseconds(3000));
        if(!sample.is_null())
        {
            auto output_buffer = r_gst_buffer(gst_buffer_copy(sample.value().buffer.get()));
            GST_BUFFER_PTS(output_buffer.get()) = sample.value().gst_pts;
            GST_BUFFER_DTS(output_buffer.get()) = sample.value().gst_dts;
            int ret;
            g_signal_emit_by_name(appsrc, "push-buffer", output_buffer.get(), &ret);
        }
    }
#endif
}

void r_recording_context::_playback_restream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media)
{
    auto prs = make_shared<playback_restreaming_state>();
    prs->rc = this;
    prs->media = media;

    tie(prs->friendly_name, prs->start_time_s, prs->end_time_s) =
        _get_playback_url_parts();
    tie(prs->con, prs->playback_duration) = 
        _fetch_contents(_ws, _camera.id, prs->start_time_s, prs->end_time_s);
    tie(prs->has_audio, prs->video_codec_name, prs->video_codec_parameters, prs->audio_codec_name, prs->audio_codec_parameters, prs->video_encoding, prs->maybe_audio_encoding) =
        _fetch_stream_info(prs, _ws, _camera.id);

    _playback_restreaming_states[media] = prs;

    auto video_caps = _create_caps(prs->video_codec_parameters, "encoded_video_sample_caps");

    auto element = gst_rtsp_media_get_element(media);
    if(!element)
        R_THROW(("Failed to get element from media in restream media configure."));

    // attach media cleanup callback to unset _live_restreaming flag
    g_object_set_data_full(G_OBJECT(media), "rtsp-extra-data", prs.get(), (GDestroyNotify)_playback_restream_cleanup_cbs);

    prs->v_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "videosrc");
    if(!prs->v_appsrc)
        R_THROW(("Failed to get videosrc from element in restream media configure."));

    gst_util_set_object_arg(G_OBJECT(prs->v_appsrc), "format", "time");

    g_object_set(G_OBJECT(prs->v_appsrc), "caps", (GstCaps*)video_caps, NULL);

    g_signal_connect(prs->v_appsrc, "need-data", (GCallback)_need_playback_data_cbs, prs.get());

    prs->a_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "audiosrc");

    if(prs->a_appsrc)
    {
        gst_util_set_object_arg(G_OBJECT(prs->a_appsrc), "format", "time");

        auto audio_caps = _create_caps(prs->audio_codec_parameters, "encoded_audio_sample_caps");

        g_object_set(G_OBJECT(prs->a_appsrc), "caps", (GstCaps*)audio_caps, NULL);

//        gst_util_set_object_arg(G_OBJECT(_a_appsrc), "stream-type", "seekable");

//        g_object_set(G_OBJECT(_v_appsrc), "duration", 10000000000, NULL);

        g_signal_connect(prs->a_appsrc, "need-data", (GCallback)_need_playback_data_cbs, prs.get());

    }
    else R_LOG_ERROR("no audio appsrc");
}

void r_recording_context::_playback_restream_cleanup_cbs(playback_restreaming_state* prs)
{
    lock_guard<mutex> g(prs->rc->_playback_restreaming_states_lok);

    auto rc = prs->rc;
    auto media = prs->media;

    rc->_playback_restreaming_states.erase(media);
}


void r_recording_context::_final_storage_writer_audio_config(const r_pipeline::sample_context& sc)
{
    auto wc = _maybe_storage_write_context.value();
    if(!sc.audio_sample_rate().is_null())
        wc.audio_codec_parameters += ", sc_audio_rate=" + r_string_utils::uint32_to_s(sc.audio_sample_rate().value());
    if(!sc.audio_channels().is_null())
        wc.audio_codec_parameters += ", sc_audio_channels=" + r_string_utils::uint32_to_s(sc.audio_channels().value());
    _maybe_storage_write_context.set_value(wc);
}

void r_recording_context::_final_storage_writer_video_config(const r_pipeline::sample_context& sc)
{
    auto wc = _maybe_storage_write_context.value();
    if(!sc.framerate().is_null())
        wc.video_codec_parameters += ", sc_framerate=" + r_string_utils::double_to_s(sc.framerate().value());

    if(wc.video_codec_name == "h264")
    {
        //, sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA
        auto sps = sc.sprop_sps().value();
        auto pps = sc.sprop_pps().value();

        wc.video_codec_parameters += string(", sprop-parameter-sets=") + sps + "," + pps;
    }
    else if(wc.video_codec_name == "h265")
    {
        //, sprop-vps=QAEMAf//AIAAAAMAAAMAAAMAAAMAALUCQA==, sprop-sps=QgEBAIAAAAMAAAMAAAMAAAMAAKACgIAtH+W1kkbQzkkktySqSfKSyA==, sprop-pps=RAHBpVgeSA==
        auto sps = sc.sprop_sps().value();
        auto pps = sc.sprop_pps().value();
        auto vps = sc.sprop_vps().value();

        wc.video_codec_parameters += string(", sprop-vps=") + vps + ", sprop-sps=" + sps + ", sprop-pps=" + pps;
    }
    _maybe_storage_write_context.set_value(wc);
}


r_storage::r_storage_write_context r_recording_context::_create_storage_writer_context()
{
    string video_codec_name, video_codec_parameters;
    int video_timebase;
    tie(video_codec_name, video_codec_parameters, video_timebase) = sdp_media_map_to_s(VIDEO_MEDIA, this->_sdp_medias);

    string audio_codec_name, audio_codec_parameters;
    int audio_timebase = 0;
    if(this->_has_audio)
        tie(audio_codec_name, audio_codec_parameters, audio_timebase) = sdp_media_map_to_s(AUDIO_MEDIA, this->_sdp_medias);

    return this->_storage_file.create_write_context(video_codec_name, video_codec_parameters, audio_codec_name, audio_codec_parameters);
}
