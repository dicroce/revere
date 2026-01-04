
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
#include "r_vss/r_query.h"
#include "r_pipeline/r_arg.h"
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_gst_buffer.h"
#include "r_av/r_muxer.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_time_utils.h"
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
    _md_storage_file(),
    _maybe_video_storage_write_context(),
    _maybe_audio_storage_write_context(),
    _last_v_time(system_clock::now()),
    _last_a_time(system_clock::now()),
    _has_audio(false),
    _stream_start_ts(),
    _stream_start_ts_set(false),
    _v_bytes_received(0),
    _a_bytes_received(0),
    _sdp_medias(),
    _video_caps(),
    _audio_caps(),
    _restream_mount_path(),
    _playback_restreaming_states_lok(),
    _playback_restreaming_states(),
    _got_first_audio_sample(false),
    _got_first_video_sample(false),
    _die(false),
    _ws(ws)
{
    // Only create metadata storage if motion detection is enabled
    if(!_camera.do_motion_detection.is_null() && _camera.do_motion_detection.value())
    {
        _md_storage_file = make_unique<r_md_storage_file>(
            top_dir + PATH_SLASH + "video" + PATH_SLASH + camera.record_file_path.value()
        );
    }

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
            if(!_stream_start_ts_set)
            {
                _stream_start_ts = system_clock::now();
                _stream_start_ts_set = true;
            }

            if(_maybe_audio_storage_write_context.is_null())
            {
                string audio_codec_name, audio_codec_parameters;
                int audio_timebase = 0;
                if(this->_has_audio)
                    tie(audio_codec_name, audio_codec_parameters, audio_timebase) = sdp_media_map_to_s(AUDIO_MEDIA, this->_sdp_medias);

                if(audio_codec_parameters.find("encoded_audio_sample_caps") == std::string::npos)
                {
                    auto acaps = _source.get_audio_caps();
                    gchar* acaps_sp = gst_caps_serialize(acaps.value(), GST_SERIALIZE_FLAG_NONE);
                    string acaps_s(acaps_sp);
                    g_free(acaps_sp);
                    auto encoded_acaps_s = r_string_utils::to_base64(acaps_s.data(), acaps_s.size());

                    audio_codec_parameters += ", encoded_audio_sample_caps=" + encoded_acaps_s;
                }

                audio_codec_parameters += ", sc_audio_rate=" + r_string_utils::uint32_to_s(sc.audio_sample_rate().value());
                audio_codec_parameters += ", sc_audio_channels=" + r_string_utils::uint32_to_s(sc.audio_channels().value());

                _maybe_audio_storage_write_context = this->_storage_file.create_write_context(audio_codec_name, audio_codec_parameters, R_STORAGE_MEDIA_TYPE_AUDIO);
                _maybe_audio_storage_write_context->codec_name = audio_codec_name;
                _maybe_audio_storage_write_context->codec_parameters = audio_codec_parameters;
            }

            if(!this->_got_first_audio_sample)
                this->_got_first_audio_sample = true;

            _last_a_time = system_clock::now();
            auto mi = buffer.map(r_gst_buffer::MT_READ);
            _a_bytes_received += mi.size();

            if(this->_audio_caps.is_null())
                this->_audio_caps = _source.get_audio_caps();

            if(!this->_video_caps.is_null() && this->_restream_mount_path.empty())
                this->_restream_mount_path = this->_sk->add_restream_mount(_sdp_medias, _camera, this, sc.video_encoding(), sc.audio_encoding());

            auto ts = (sc.stream_start_ts() + pts);
            this->_storage_file.write_frame(
                this->_maybe_audio_storage_write_context.value(),
                R_STORAGE_MEDIA_TYPE_AUDIO,
                mi.data(),
                mi.size(),
                key,
                ts,
                pts
            );

            this->_sk->iterate_live_restreaming_states(this->_camera.id, [&](live_restreaming_state& lrs) {
                if(lrs.live_restream_key_sent)
                {
                    if(!lrs.first_restream_a_times_set)
                    {
                        lrs.first_restream_a_times_set = true;
                        lrs.first_restream_a_pts = pts * 1000000;
                        lrs.first_restream_a_dts = pts * 1000000;
                    }

                    _frame_context fc;
                    fc.gst_pts = (pts * 1000000) - lrs.first_restream_a_pts;
                    fc.gst_dts = (pts * 1000000) - lrs.first_restream_a_dts;

                    fc.key = key;
                    fc.buffer = buffer;
                    lrs.audio_samples.post(fc);
                }
            });
        }
        catch(exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
            //_die = true;
        }
    });

    _source.set_video_sample_cb([this](const sample_context& sc, const r_gst_buffer& buffer, bool key, int64_t pts){

        try
        {
            if(!_stream_start_ts_set)
            {
                _stream_start_ts = system_clock::now();
                _stream_start_ts_set = true;
            }

            if(_maybe_video_storage_write_context.is_null())
            {
                string video_codec_name, video_codec_parameters;
                int video_timebase;
                tie(video_codec_name, video_codec_parameters, video_timebase) = sdp_media_map_to_s(VIDEO_MEDIA, this->_sdp_medias);

                if(video_codec_parameters.find("encoded_video_sample_caps") == std::string::npos)
                {
                    auto vcaps = _source.get_video_caps();
                    gchar* vcaps_sp = gst_caps_serialize(vcaps.value(), GST_SERIALIZE_FLAG_NONE);
                    string vcaps_s(vcaps_sp);
                    g_free(vcaps_sp);
                    auto encoded_vcaps_s = r_string_utils::to_base64(vcaps_s.data(), vcaps_s.size());
                    video_codec_parameters += ", encoded_video_sample_caps=" + encoded_vcaps_s;
                }

                if(!sc.framerate().is_null())
                    video_codec_parameters += ", sc_framerate=" + r_string_utils::double_to_s(sc.framerate().value());

                if(video_codec_name == "h264")
                {
                    //, sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA
                    auto maybe_sps = sc.sprop_sps();
                    auto maybe_pps = sc.sprop_pps();

                    string param_sets;
                    if(!maybe_sps.is_null())
                        param_sets = maybe_sps.value();
                    if(!maybe_pps.is_null())
                    {
                        param_sets += ",";
                        param_sets += maybe_pps.value();
                    }

                    if(!param_sets.empty())
                        video_codec_parameters += string(", sprop-parameter-sets=") + param_sets;
                }
                else if(video_codec_name == "h265")
                {
                    //, sprop-vps=QAEMAf//AIAAAAMAAAMAAAMAAAMAALUCQA==, sprop-sps=QgEBAIAAAAMAAAMAAAMAAAMAAKACgIAtH+W1kkbQzkkktySqSfKSyA==, sprop-pps=RAHBpVgeSA==

                    auto maybe_sps = sc.sprop_sps();
                    auto maybe_pps = sc.sprop_pps();
                    auto maybe_vps = sc.sprop_vps();

                    if(!maybe_sps.is_null())
                        video_codec_parameters += string(", sprop-sps=") + maybe_sps.value();
                    
                    if(!maybe_pps.is_null())
                        video_codec_parameters += string(", sprop-pps=") + maybe_pps.value();
                    
                    if(!maybe_vps.is_null())
                        video_codec_parameters += string(", sprop-vps=") + maybe_vps.value();
                }

                _maybe_video_storage_write_context = this->_storage_file.create_write_context(video_codec_name, video_codec_parameters, R_STORAGE_MEDIA_TYPE_VIDEO);
                _maybe_video_storage_write_context->codec_name = video_codec_name;
                _maybe_video_storage_write_context->codec_parameters = video_codec_parameters;
            }

            if(!this->_got_first_video_sample)
                this->_got_first_video_sample = true;

            _last_v_time = system_clock::now();
            auto mi = buffer.map(r_gst_buffer::MT_READ);
            _v_bytes_received += mi.size();
            if(this->_video_caps.is_null())
            {
                this->_video_caps = _source.get_video_caps();
                if(!this->_has_audio)
                    this->_restream_mount_path = this->_sk->add_restream_mount(_sdp_medias, _camera, this, sc.video_encoding(), sc.audio_encoding());
            }

            auto ts = (sc.stream_start_ts() + pts);
            this->_storage_file.write_frame(
                this->_maybe_video_storage_write_context.value(),
                R_STORAGE_MEDIA_TYPE_VIDEO,
                mi.data(),
                mi.size(),
                key,
                ts,
                pts
            );

            bool do_motion = (!this->_camera.do_motion_detection.is_null())?this->_camera.do_motion_detection.value():false;

            if(do_motion)
            {
                this->_sk->post_frame_to_motion_engine(
                    buffer,
                    ts,
                    this->_maybe_video_storage_write_context.value().codec_name,
                    this->_maybe_video_storage_write_context.value().codec_parameters,
                    this->_camera.id,
                    key || GST_BUFFER_FLAG_IS_SET(buffer.get(), GST_BUFFER_FLAG_NON_DROPPABLE)
                );
            }

            this->_sk->iterate_live_restreaming_states(this->_camera.id, [&](live_restreaming_state& lrs) {
                if(lrs.live_restream_key_sent || key)
                {
                    lrs.live_restream_key_sent = true;

                    if(!lrs.first_restream_v_times_set)
                    {
                        lrs.first_restream_v_times_set = true;
                        lrs.first_restream_v_pts = pts * 1000000;
                        lrs.first_restream_v_dts = pts * 1000000;
                    }

                    _frame_context fc;
                    fc.gst_pts = (pts*1000000) - lrs.first_restream_v_pts;
                    fc.gst_dts = (pts*1000000) - lrs.first_restream_v_dts;
                    fc.key = key;
                    fc.buffer = buffer;
                    lrs.video_samples.post(fc);
                }
            });
        }
        catch(exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
            //_die = true;
        }
    });

    _source.set_sdp_media_cb([this](const map<string, r_sdp_media>& sdp_medias){
        this->_sdp_medias = sdp_medias;
        if(this->_sdp_medias.find("audio") != this->_sdp_medias.end())
            this->_has_audio = true;
    });

    _source.play();
}

r_recording_context::~r_recording_context() noexcept
{
    _sk->remove_restream_mount(_restream_mount_path);

    _source.stop();

    // Explicitly clear write contexts BEFORE _storage_file is destroyed
    // This ensures nanots write_context unique_ptrs are destroyed immediately,
    // releasing the stream tags ("video" and "audio") before a new recording
    // context can attempt to reuse them. This prevents the race condition where
    // a dying stream's write contexts still exist when creating a new stream.

    if(!_maybe_video_storage_write_context.is_null())
        _maybe_video_storage_write_context.clear();

    if(!_maybe_audio_storage_write_context.is_null())
        _maybe_audio_storage_write_context.clear();
}

bool r_recording_context::dead() const
{
    auto now = system_clock::now();
    auto video_dead = ((now - _last_v_time) > seconds(20));
    bool is_dead = (_has_audio)?((now - _last_a_time) > seconds(20))||video_dead:video_dead;
    if(_die)
        is_dead = true;

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

r_md_storage_file& r_recording_context::metadata_storage()
{
    if(!_md_storage_file)
        R_THROW(("Metadata storage not available - motion detection is disabled"));
    return *_md_storage_file;
}

void r_recording_context::write_metadata(const std::string& stream_tag, const std::string& json_data, int64_t timestamp_ms)
{
    if(!_md_storage_file)
        return;  // No metadata storage when motion detection disabled

    R_LOG_INFO("[RECORDING_CONTEXT] write_metadata stream_tag=%s ts=%lld json=%s",
               stream_tag.c_str(), (long long)timestamp_ms, json_data.c_str());
    _md_storage_file->write_metadata(stream_tag, json_data, timestamp_ms);
}

static void _need_live_data_cbs(GstElement* appsrc, guint, live_restreaming_state* lrs)
{
    if(appsrc == lrs->v_appsrc)
    {
        auto sample = lrs->video_samples.poll(chrono::milliseconds(3000));
        if(!sample.is_null())
        {
            GstBuffer* output_buffer = gst_buffer_copy(sample.value().buffer.get());
            GST_BUFFER_PTS(output_buffer) = sample.value().gst_pts;
            GST_BUFFER_DTS(output_buffer) = sample.value().gst_dts;
            int ret;
            g_signal_emit_by_name(appsrc, "push-buffer", output_buffer, &ret);
            gst_buffer_unref(output_buffer);
        }
    }
    else
    {
        auto sample = lrs->audio_samples.poll(chrono::milliseconds(3000));
        if(!sample.is_null())
        {
            GstBuffer* output_buffer = gst_buffer_copy(sample.value().buffer.get());
            GST_BUFFER_PTS(output_buffer) = sample.value().gst_pts;
            GST_BUFFER_DTS(output_buffer) = sample.value().gst_dts;
            int ret;
            g_signal_emit_by_name(appsrc, "push-buffer", output_buffer, &ret);
            gst_buffer_unref(output_buffer);
        }
    }
}

void r_recording_context::live_restream_media_configure(GstRTSPMediaFactory*, GstRTSPMedia* media)
{
    auto lrs = make_shared<live_restreaming_state>();
    lrs->rc = this;
    lrs->sk = _sk;  // Store stream_keeper pointer for safe cleanup
    lrs->media = media;
    lrs->camera_id = _camera.id;

    auto element = gst_rtsp_media_get_element(media);
    if(!element)
        R_THROW(("Failed to get element from media in restream media configure."));

    // pay0 is the video payloader
    // pay1 is the audio payloader which might now actually be present

    // attach media cleanup callback - now safe because r_stream_keeper outlives r_recording_context
    g_object_set_data_full(G_OBJECT(media), "rtsp-extra-data", lrs.get(), (GDestroyNotify)_live_restream_cleanup_cbs);

    lrs->v_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "videosrc");
    if(!lrs->v_appsrc)
        R_THROW(("Failed to get videosrc from element in restream media configure."));

    gst_util_set_object_arg(G_OBJECT(lrs->v_appsrc), "format", "time");

    g_object_set(G_OBJECT(lrs->v_appsrc), "caps", (GstCaps*)_video_caps.value(), NULL);

    g_signal_connect(lrs->v_appsrc, "need-data", (GCallback)_need_live_data_cbs, lrs.get());

    lrs->a_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "audiosrc");

    if(lrs->a_appsrc)
    {
        gst_util_set_object_arg(G_OBJECT(lrs->a_appsrc), "format", "time");

        g_object_set(G_OBJECT(lrs->a_appsrc), "caps", (GstCaps*)_audio_caps.value(), NULL);

        g_signal_connect(lrs->a_appsrc, "need-data", (GCallback)_need_live_data_cbs, lrs.get());
    }

    // Store in r_stream_keeper's map (not r_recording_context's)
    _sk->add_live_restreaming_state(media, lrs);
}

void r_recording_context::stop()
{
    _source.stop();
}

void r_recording_context::create_playback_mount(GstRTSPServer*, GstRTSPMountPoints* mounts, const std::string& url, uint64_t start_ts, uint64_t end_ts)
{
    system_clock::time_point start_tp{milliseconds{start_ts}};
    system_clock::time_point end_tp{milliseconds{end_ts}};

    auto contents = query_get_contents(_top_dir, _sk->get_devices(), _camera.id, start_tp, end_tp);

    if(contents.segments.empty())
    {
        R_LOG_ERROR("no segments found for playback restream mount: camera.friendly_name=%s, start_ts=%s, end_ts=%s", _camera.friendly_name.value().c_str(), r_string_utils::uint64_to_s(start_ts).c_str(), r_string_utils::uint64_to_s(end_ts).c_str());
        return;
    }

    auto first_segment = contents.segments.front();

    auto video_buffer = query_get_video(_top_dir, _sk->get_devices(), _camera.id, first_segment.start, first_segment.start + seconds(5));

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

    gst_rtsp_mount_points_add_factory(mounts, url.c_str(), factory);
}

tuple<string, system_clock::time_point, system_clock::time_point> r_recording_context::_get_playback_url_parts()
{
    GstRTSPContext* context = gst_rtsp_context_get_current();

    auto klass = GST_RTSP_CLIENT_GET_CLASS(context->client);
    shared_ptr<gchar> path_p(klass->make_path_from_uri(context->client, context->uri), [](gchar* p){if(p) g_free(p);});
    string path(path_p.get());

    if(r_string_utils::starts_with(path, "/"))
        path = path.substr(1);

    auto parts = r_string_utils::split(path, '_');

    // playback urls look like this: the_porch_2024-12-10T12:00:00.000Z_2024-12-10T13:00:00.000Z
    if(parts.size() < 3)
        R_THROW(("Unable to parse playback url (%s)!", path.c_str()));

    auto end_time_s = parts.back();
    parts.pop_back();

    auto start_time_s = parts.back();
    parts.pop_back();

    string friendly_name = r_string_utils::join(parts, '_');

    return make_tuple(friendly_name, r_time_utils::iso_8601_to_tp(start_time_s), r_time_utils::iso_8601_to_tp(end_time_s));
}

void r_recording_context::_live_restream_cleanup_cbs(live_restreaming_state* lrs)
{
    // This callback is called by GStreamer when the media object is destroyed.
    // It's now safe to access lrs->sk because r_stream_keeper outlives all
    // r_recording_context instances and the GStreamer main loop.
    // The shared_ptr in the map keeps the lrs alive until we erase it.
    lrs->sk->remove_live_restreaming_state(lrs->media);
}

static tuple<bool, string, string, string, string, r_encoding, r_nullable<r_encoding>>
_fetch_stream_info(shared_ptr<playback_restreaming_state> prs, const string& camera_id)
{
    if(prs->con.segments.empty())
        R_THROW(("No segments found for playback restream mount."));

    auto first_segment = prs->con.segments.front();

    auto video_buffer = query_get_video(prs->top_dir, prs->devices, camera_id, first_segment.start, first_segment.start + seconds(5));

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

static tuple<contents, std::chrono::milliseconds> _fetch_contents(const string& top_dir, r_disco::r_devices& devices, const string& camera_id, system_clock::time_point start_time, system_clock::time_point end_time)
{
    auto con = query_get_contents(top_dir, devices, camera_id, start_time, end_time);

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

static r_gst_caps _create_caps(const string& codec_parameters, const string& key)
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

static void _need_playback_data_cbs(GstElement* appsrc, guint, playback_restreaming_state* prs)
{
    if(appsrc == prs->v_appsrc)
    {
        auto sample = prs->video_samples.poll(chrono::milliseconds(3000));
        if(!sample.is_null())
        {
            auto output_buffer = sample.value().buffer;
            GST_BUFFER_PTS(output_buffer.get()) = sample.value().gst_pts;
            GST_BUFFER_DTS(output_buffer.get()) = sample.value().gst_dts;
            int ret;
            g_signal_emit_by_name(appsrc, "push-buffer", output_buffer.get(), &ret);
        }
    }
    else
    {
        auto sample = prs->audio_samples.poll(chrono::milliseconds(3000));
        if(!sample.is_null())
        {
            auto output_buffer = sample.value().buffer;
            GST_BUFFER_PTS(output_buffer.get()) = sample.value().gst_pts;
            GST_BUFFER_DTS(output_buffer.get()) = sample.value().gst_dts;
            int ret;
            g_signal_emit_by_name(appsrc, "push-buffer", output_buffer.get(), &ret);

        }
    }
}

static bool _time_to_get_more_data(shared_ptr<playback_restreaming_state> prs)
{
    return prs->running && (prs->video_samples.size() < 40 || (prs->has_audio && prs->audio_samples.size() < 40));
}

static void _playback_entry_point(shared_ptr<playback_restreaming_state> prs)
{
    prs->running = true;

    while(prs->running)
    {
        try
        {
            if(_time_to_get_more_data(prs))
            {
                auto video_buffer = query_get_video(prs->top_dir, prs->devices, prs->camera_id, prs->query_start, prs->query_end);

                prs->query_start = prs->query_end;
                prs->query_end = prs->query_start + seconds(5);

                uint32_t version = 0;
                auto bt = r_blob_tree::deserialize(video_buffer.data(), video_buffer.size(), version);

                if(bt.has_key("frames"))
                {
                    auto n_frames = bt["frames"].size();

                    for(size_t fi = 0; fi < n_frames; ++fi)
                    {
                        auto sid = bt["frames"][fi]["stream_id"].get_value<int>();
                        auto key = (bt["frames"][fi]["key"].get_string() == "true");
                        auto frame = bt["frames"][fi]["data"].get_blob();
                        auto ts = bt["frames"][fi]["ts"].get_value<int64_t>();

                        if(prs->first_ts.is_null())
                            prs->first_ts.set_value(ts);

                        if(system_clock::time_point(milliseconds(ts)) > prs->end_time)
                        {
                            prs->running = false;
                            break;
                        }

                        r_gst_buffer buffer(frame.data(), frame.size());

                        _frame_context fc;
                        fc.gst_pts = (ts - prs->first_ts.value()) * 1000000;
                        fc.gst_dts = (ts - prs->first_ts.value()) * 1000000;
                        fc.key = key;                
                        fc.buffer = buffer;

                        if(sid == R_STORAGE_MEDIA_TYPE_VIDEO)
                            prs->video_samples.post(fc);
                        else if(sid == R_STORAGE_MEDIA_TYPE_AUDIO)
                            prs->audio_samples.post(fc);
                    }
                }
            }
            else this_thread::sleep_for(chrono::milliseconds(200));
        }
        catch(const std::exception& e)
        {
            R_LOG_EXCEPTION(e);
        }
    }
}

void r_recording_context::_playback_restream_media_configure(GstRTSPMediaFactory*, GstRTSPMedia* media)
{
    auto prs = make_shared<playback_restreaming_state>(_top_dir, _sk->get_devices());
    prs->rc = this;
    prs->media = media;

    tie(prs->friendly_name, prs->start_time, prs->end_time) =
        _get_playback_url_parts();

    prs->query_start = prs->start_time;
    prs->query_end = prs->start_time + seconds(5);
    prs->camera_id = _camera.id;

    tie(prs->con, prs->playback_duration) = 
        _fetch_contents(_top_dir, _sk->get_devices(), _camera.id, prs->start_time, prs->end_time);

    tie(prs->has_audio, prs->video_codec_name, prs->video_codec_parameters, prs->audio_codec_name, prs->audio_codec_parameters, prs->video_encoding, prs->maybe_audio_encoding) =
        _fetch_stream_info(prs, _camera.id);

    auto element = gst_rtsp_media_get_element(media);
    if(!element)
        R_THROW(("Failed to get element from media in restream media configure."));

    // attach media cleanup callback to unset _live_restreaming flag
    g_object_set_data_full(G_OBJECT(media), "rtsp-extra-data", prs.get(), (GDestroyNotify)_playback_restream_cleanup_cbs);

    prs->v_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "videosrc");
    if(!prs->v_appsrc)
        R_THROW(("Failed to get videosrc from element in restream media configure."));

    gst_util_set_object_arg(G_OBJECT(prs->v_appsrc), "format", "time");

    auto video_caps = _create_caps(prs->video_codec_parameters, "encoded_video_sample_caps");

    g_object_set(G_OBJECT(prs->v_appsrc), "caps", (GstCaps*)video_caps, NULL);

//    gst_util_set_object_arg(G_OBJECT(prs->v_appsrc), "stream-type", "seekable");

    g_object_set(G_OBJECT(prs->v_appsrc), "duration", chrono::duration_cast<chrono::nanoseconds>(prs->playback_duration).count(), NULL);

    g_signal_connect(prs->v_appsrc, "need-data", (GCallback)_need_playback_data_cbs, prs.get());

    prs->a_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "audiosrc");

    if(prs->a_appsrc)
    {
        gst_util_set_object_arg(G_OBJECT(prs->a_appsrc), "format", "time");

        auto audio_caps = _create_caps(prs->audio_codec_parameters, "encoded_audio_sample_caps");

        g_object_set(G_OBJECT(prs->a_appsrc), "caps", (GstCaps*)audio_caps, NULL);

//        gst_util_set_object_arg(G_OBJECT(prs->a_appsrc), "stream-type", "seekable");

        g_object_set(G_OBJECT(prs->a_appsrc), "duration", chrono::duration_cast<chrono::nanoseconds>(prs->playback_duration).count(), NULL);

        g_signal_connect(prs->a_appsrc, "need-data", (GCallback)_need_playback_data_cbs, prs.get());

//        g_signal_connect(prs->a_appsrc, "seek-data", (GCallback)_seek_playback_data_cbs, prs.get());
    }

    prs->playback_thread = std::thread(
        _playback_entry_point,
        prs
    );

    // Lock must be held when inserting to prevent race with cleanup callback erase
    {
        std::lock_guard<std::mutex> lock(_playback_restreaming_states_lok);
        _playback_restreaming_states[media] = prs;
    }
}

void r_recording_context::_playback_restream_cleanup_cbs(playback_restreaming_state* prs)
{
    lock_guard<mutex> g(prs->rc->_playback_restreaming_states_lok);

    auto rc = prs->rc;
    auto media = prs->media;

    prs->running = false;
    prs->playback_thread.join();

    rc->_playback_restreaming_states.erase(media);
}
