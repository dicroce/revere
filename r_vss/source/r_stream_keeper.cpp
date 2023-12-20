
#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_recording_context.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_std_utils.h"
#include "r_utils/r_functional.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_file.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_time_utils.h"

#include <algorithm>

using namespace r_vss;
using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace r_disco;
using namespace std;

r_stream_keeper::r_stream_keeper(r_devices& devices, const string& top_dir) :
    _devices(devices),
    _top_dir(top_dir),
    _th(),
    _running(false),
    _streams(),
    _cmd_q(),
    _rtsp_server_th(),
    _loop(g_main_loop_new(NULL, FALSE)),
    _server(gst_rtsp_onvif_server_new()),
    _mounts(nullptr),
    _factories(),
    _motionEngine(_devices, top_dir),
    _ws(top_dir, _devices),
    _prune(_ws)
{
    auto video_path = top_dir + PATH_SLASH + "video";

    if(!r_fs::file_exists(video_path))
        r_fs::mkdir(video_path);

    g_object_set(_server, "service", to_string(10554).c_str(), NULL);

    _mounts = gst_rtsp_server_get_mount_points(_server);

    gst_rtsp_server_attach(_server, NULL);

    _motionEngine.start();
}

r_stream_keeper::~r_stream_keeper() noexcept
{
    if(_running)
        stop();

    _motionEngine.stop();

    for(auto f : _factories)
        g_object_unref(f);

    if(_mounts)
        g_object_unref(_mounts);

    if(_server)
        g_object_unref(_server);

    if(_loop)
        g_main_loop_unref(_loop);
}

void r_stream_keeper::start()
{
    if(_running)
        R_THROW(("Stream keeper already started!"));

    _running = true;

    g_signal_connect(
        _server,
        "client-connected",
        (GCallback)_client_connected_cbs,
        this
    );

    _rtsp_server_th = thread(&r_stream_keeper::_rtsp_server_entry_point, this);

    _th = thread(&r_stream_keeper::_entry_point, this);

    _prune.start();
}

void r_stream_keeper::stop()
{
    if(!_running)
        R_THROW(("Cannot stop stream keeper if its not running!"));

    for(auto& s : _streams)
        s.second->stop();

    _motionEngine.stop();

    _running = false;

    g_main_loop_quit(_loop);

    _th.join();

    _rtsp_server_th.join();

    _prune.stop();
}

vector<r_stream_status> r_stream_keeper::fetch_stream_status()
{
    r_stream_keeper_cmd cmd;
    cmd.cmd = R_SK_FETCH_STREAM_STATUS;

    return _cmd_q.post(cmd).get().stream_infos;
}

bool r_stream_keeper::is_recording(const string& id)
{
    r_stream_keeper_cmd cmd;
    cmd.cmd = R_SK_IS_RECORDING;
    cmd.id = id;

    return _cmd_q.post(cmd).get().is_recording;
}

std::string r_stream_keeper::add_restream_mount(const std::map<std::string, r_pipeline::r_sdp_media>& sdp_medias, const r_disco::r_camera& camera, r_recording_context* rc, r_nullable<r_pipeline::r_encoding> video_encoding, r_nullable<r_pipeline::r_encoding> audio_encoding)
{
    auto factory = gst_rtsp_media_factory_new();
    if(!factory)
        R_THROW(("Failed to create restream media factory!"));

    // fetch video encoding (it's mandatory)
    if(sdp_medias.find("video") == sdp_medias.end())
        R_THROW(("No video stream found in SDP!"));

    auto& sdp_video = sdp_medias.at("video");

    if(sdp_video.formats.empty())
        R_THROW(("No video formats found in SDP!"));

    auto& sdp_video_formats = sdp_video.formats;

    // fetch audio encoding (it's not mandatory)
    r_nullable<r_pipeline::r_sdp_media> sdp_audio;

    if(sdp_medias.find("audio") != sdp_medias.end())
    {
        sdp_audio.set_value(sdp_medias.at("audio"));

        if(sdp_audio.value().formats.empty())
            R_THROW(("No audio formats found in SDP!"));
    }

    auto launch_str = create_restream_launch_string(video_encoding.value(), sdp_video.formats.front(), audio_encoding, (audio_encoding.is_null())?0:sdp_audio.value().formats.front());

    gst_rtsp_media_factory_set_launch(factory, launch_str.c_str());

    gst_rtsp_media_factory_set_shared(factory, FALSE);

    g_signal_connect(factory, "media-configure", (GCallback)_live_restream_media_configure, rc);

    if(camera.friendly_name.is_null())
        R_THROW(("Camera friendly name is null!"));

    string mount_path = camera.friendly_name.value();

    replace(begin(mount_path), end(mount_path), ' ', '_');

    mount_path = r_string_utils::format("/%s", mount_path.c_str());

    gst_rtsp_mount_points_add_factory(_mounts, mount_path.c_str(), factory);

    return mount_path;
}

void r_stream_keeper::remove_restream_mount(const string& mount_path)
{
    gst_rtsp_mount_points_remove_factory(_mounts, mount_path.c_str());

    raii_ptr<GstRTSPSessionPool> pool(
        gst_rtsp_server_get_session_pool(_server),
        [](GstRTSPSessionPool* p){g_object_unref(p);}
    );

    raii_ptr<GList> session_list(
        gst_rtsp_session_pool_filter(pool.get(), NULL, NULL),
        [](GList* l){g_list_free(l);}
    );

    GList* session = session_list.get();
    for(; session; session = session->next)
    {
        GstRTSPSession* sess = (GstRTSPSession*)session->data;
        int matched;
        GstRTSPSessionMedia* sess_media = gst_rtsp_session_get_media(sess, mount_path.c_str(), &matched);
        if(matched == (int)mount_path.size())
        {
            GstRTSPMedia* media = gst_rtsp_session_media_get_media(sess_media);
            g_object_ref(media);
            gst_rtsp_media_lock(media);
            gst_rtsp_session_media_set_state(sess_media, GST_STATE_NULL);
            gst_rtsp_session_release_media(sess, sess_media);
            gst_rtsp_media_unlock(media);
            g_object_unref(media);
        }
    }
}

void r_stream_keeper::post_key_frame_to_motion_engine(r_pipeline::r_gst_buffer buffer, int64_t ts, const std::string& video_codec_name, const std::string& video_codec_params, const std::string& camera_id)
{
    _motionEngine.post_frame(buffer, ts, video_codec_name, video_codec_params, camera_id);
}

vector<uint8_t> r_stream_keeper::get_jpg(const string& camera_id, int64_t ts, uint16_t w, uint16_t h)
{
    return _ws.get_jpg(camera_id, r_time_utils::epoch_millis_to_tp(ts), w, h);
}

std::chrono::hours r_stream_keeper::get_retention_hours(const string& camera_id)
{
    return _ws.get_retention_hours(camera_id);
}

void r_stream_keeper::bounce(const std::string& camera_id)
{
    r_stream_keeper_cmd cmd;
    cmd.cmd = R_SK_STOP;
    cmd.id = camera_id;

    _cmd_q.post(cmd).get();
}

string r_stream_keeper::create_restream_launch_string(r_pipeline::r_encoding video_encoding, int video_format, r_utils::r_nullable<r_pipeline::r_encoding> maybe_audio_encoding, int audio_format)
{
    string launch_str = r_string_utils::format("( appsrc name=videosrc ! ");

    if(video_encoding == r_pipeline::r_encoding::H264_ENCODING)
        launch_str += r_string_utils::format("h264parse config-interval=-1 ! rtph264pay name=pay0 pt=%d ", video_format);
    else if(video_encoding == r_pipeline::r_encoding::H265_ENCODING)
        launch_str += r_string_utils::format("h265parse config-interval=-1 ! rtph265pay name=pay0 pt=%d ", video_format);
    else
        R_THROW(("Unsupported video encoding: %s", r_pipeline::encoding_to_str(video_encoding).c_str()));

    if(!maybe_audio_encoding.is_null())
    {
        launch_str += string(" appsrc name=audiosrc ! ");

        if(maybe_audio_encoding.value() == r_pipeline::r_encoding::AAC_GENERIC_ENCODING)
            launch_str += r_string_utils::format("aacparse ! rtpmp4gpay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::AAC_LATM_ENCODING)
            launch_str += r_string_utils::format("aacparse ! rtpmp4apay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::PCMU_ENCODING)
            launch_str += r_string_utils::format("rawaudioparse ! rtppcmupay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::PCMA_ENCODING)
            launch_str += r_string_utils::format("rawaudioparse ! rtppcmapay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::AAL2_G726_16_ENCODING)
            launch_str += r_string_utils::format("rtpg726pay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::AAL2_G726_24_ENCODING)
            launch_str += r_string_utils::format("rtpg726pay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::AAL2_G726_32_ENCODING)
            launch_str += r_string_utils::format("rtpg726pay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::AAL2_G726_40_ENCODING)
            launch_str += r_string_utils::format("rtpg726pay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::G726_16_ENCODING)
            launch_str += r_string_utils::format("rtpg726pay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::G726_24_ENCODING)
            launch_str += r_string_utils::format("rtpg726pay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::G726_32_ENCODING)
            launch_str += r_string_utils::format("rtpg726pay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::G726_40_ENCODING)
            launch_str += r_string_utils::format("rtpg726pay name=pay1 pt=%d )", audio_format);
        else R_THROW(("Unsupported audio encoding: %s", r_pipeline::encoding_to_str(maybe_audio_encoding.value()).c_str()));
    }
    else launch_str += ")";

    return launch_str;
}

void r_stream_keeper::_entry_point()
{
    while(_running)
    {
        try
        {
            if(_streams.empty())
                _add_recording_contexts(_devices.get_assigned_cameras());
            else
            {
                auto cameras = _get_current_cameras();

                _remove_recording_contexts(_devices.get_modified_cameras(cameras));
                _remove_recording_contexts(_devices.get_assigned_cameras_removed(cameras));
                _add_recording_contexts(_devices.get_assigned_cameras_added(_get_current_cameras()));
            }

            // remove dead recording contexts...
            r_funky::erase_if(_streams, [](const auto& c){return c.second->dead();});

            auto c = _cmd_q.poll(std::chrono::seconds(2));

            if(!c.is_null())
            {
                auto cmd = move(c.take());

                if(cmd.first.cmd == R_SK_FETCH_STREAM_STATUS)
                {
                    r_stream_keeper_result result;
                    result.stream_infos = _fetch_stream_status();
                    cmd.second.set_value(result);
                }
                else if(cmd.first.cmd == R_SK_IS_RECORDING)
                {
                    r_stream_keeper_result result;
                    result.is_recording = _streams.find(cmd.first.id) != _streams.end();
                    cmd.second.set_value(result);
                }
                else if(cmd.first.cmd == R_SK_STOP)
                {
                    r_stream_keeper_result result;
                    _stop(cmd.first.id);
                    cmd.second.set_value(result);
                }
                else if(cmd.first.cmd == R_SK_CREATE_PLAYBACK_MOUNT)
                {
                    r_stream_keeper_result result;
                    _create_playback_mount(cmd.first.friendly_name, cmd.first.url, cmd.first.start_ts, cmd.first.end_ts);
                    cmd.second.set_value(result);
                }
                else R_THROW(("Unknown command sent to stream keeper!"));
            }
        }
        catch(const std::exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
        }
    }
}

void r_stream_keeper::_rtsp_server_entry_point()
{
    while(_running)
    {
        g_main_context_iteration(NULL, true);
    }
}

vector<r_camera> r_stream_keeper::_get_current_cameras()
{
    vector<r_camera> cameras;
    cameras.reserve(_streams.size());
    for(auto& c : _streams)
        cameras.push_back(c.second->camera());
    return cameras;
}

void r_stream_keeper::_add_recording_contexts(const vector<r_camera>& cameras)
{
    for(const auto& camera : cameras)
    {
        if(_streams.count(camera.id) == 0)
        {
            printf("stream keeper add camera.id=%s\n", camera.id.c_str());
            fflush(stdout);
            _streams[camera.id] = make_shared<r_recording_context>(this, camera, _top_dir, _ws);
        }
    }
}

void r_stream_keeper::_remove_recording_contexts(const std::vector<r_disco::r_camera>& cameras)
{
    for(const auto& camera : cameras)
    {
        if(_streams.count(camera.id) > 0)
            _streams.erase(camera.id);
    }
}

vector<r_stream_status> r_stream_keeper::_fetch_stream_status() const
{
    vector<r_stream_status> statuses;
    statuses.reserve(_streams.size());

    transform(
        _streams.begin(),
        _streams.end(),
        back_inserter(statuses),
        [](const auto& c){
            r_stream_status s;
            s.camera = c.second->camera();
            s.bytes_per_second = c.second->bytes_per_second();
            return s;
        }
    );

    return statuses;
}

void r_stream_keeper::_live_restream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media, gpointer user_data)
{
    ((r_recording_context*)user_data)->live_restream_media_configure(factory, media);
}

void r_stream_keeper::_stop(const string& camera_id)
{
    if(_streams.count(camera_id) == 0)
    {
        R_LOG_ERROR("Attempt to stop recording for camera %s that is not being recorded!", camera_id.c_str());
        return;
    }

    _streams[camera_id]->stop();
}

void r_stream_keeper::_client_connected_cbs(GstRTSPServer* server, GstRTSPClient* client, r_stream_keeper* sk)
{
    g_signal_connect(
        client,
        "options-request",
        (GCallback)_options_cbs,
        sk
    );
}

void r_stream_keeper::_options_cbs(GstRTSPClient* client, GstRTSPContext* ctx, r_stream_keeper* sk)
{
    sk->_options_cb(client, ctx);
}

void r_stream_keeper::_options_cb(GstRTSPClient* client, GstRTSPContext* ctx)
{
    GstRTSPClientClass* klass = GST_RTSP_CLIENT_GET_CLASS(client);
    shared_ptr<gchar> raw_path(klass->make_path_from_uri(client, ctx->uri), [](gchar* p){if(p) g_free(p);});
    auto path = string(raw_path.get());

    if(r_string_utils::starts_with(path, "/"))
        path = path.substr(1);

    auto parts = r_string_utils::split(path, '_');

    // playback urls look like this: the_porch_2024-12-10T12:00:00.000Z_2024-12-10T13:00:00.000Z
    if(parts.size() < 3)
        return;

    auto end_time_s = parts.back();
    parts.pop_back();

    auto start_time_s = parts.back();
    parts.pop_back();

    string friendly_name = r_string_utils::join(parts, '_');

    // Now, we have the friendly name, start time, and end time. Ultimately we need to send
    // this request to the proper r_recording_context, but to do that we need to access _streams
    // but that all happens on the thread processing our command q. So, we'll post a command to 
    // it to take it from here.

    r_stream_keeper_cmd cmd;
    cmd.cmd = R_SK_CREATE_PLAYBACK_MOUNT;
    cmd.friendly_name = friendly_name;
    cmd.start_ts = r_time_utils::tp_to_epoch_millis(r_time_utils::iso_8601_to_tp(start_time_s));
    cmd.end_ts = r_time_utils::tp_to_epoch_millis(r_time_utils::iso_8601_to_tp(end_time_s));
    cmd.url = "/" + path;

    // Why wait here? Because we need to wait for the mount to be created before we can send the response
    // because the client might get their DESCRIBE request in before the mount is created and then we'll
    // get a failure. We dont wait forever here however, just in case (also note, wait_for() will return
    // immediately when the response is ready... so in most cases this wait will be very short).
    _cmd_q.post(cmd).wait_for(chrono::seconds(1));
}

void r_stream_keeper::_create_playback_mount(const string& friendly_name, const string& url, uint64_t start_ts, uint64_t end_ts)
{
    for(auto s : _streams)
    {
        if(s.second->camera().friendly_name.value() == friendly_name)
        {
            s.second->create_playback_mount(_server, _mounts, url, start_ts, end_ts);
            return;
        }
    }
}
