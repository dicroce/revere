#include "r_vss/r_playback_restream.h"
#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_live_restream_host.h"
#include "r_vss/r_rtsp_auth.h"
#include "r_storage/r_storage_file.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_time_utils.h"
#include <memory>
#include <tuple>

using namespace r_vss;
using namespace r_disco;
using namespace r_pipeline;
using namespace r_storage;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

namespace
{

// What a playback mount plays: one camera's recording. Owned by the mount's
// factory (freed with its media-configure handler).
struct playback_mount
{
    string top_dir;
    r_devices* devices {nullptr};
    string camera_id;
};

tuple<string, system_clock::time_point, system_clock::time_point> _get_playback_url_parts()
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

tuple<bool, string, string, string, string, r_encoding, r_nullable<r_encoding>>
_fetch_stream_info(playback_restreaming_state* prs, const string& camera_id)
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
    {
        auto audio_name = bt["audio_codec_name"].get_string();
        if(!audio_name.empty())
        {
            try { maybe_audio_encoding.set_value(r_pipeline::str_to_encoding(audio_name)); }
            catch(const std::exception& e) { R_LOG_WARNING("Unrecognized audio codec '%s' in stored segment, treating as no audio: %s", audio_name.c_str(), e.what()); }
        }
    }

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

tuple<contents, milliseconds> _fetch_contents(const string& top_dir, r_devices& devices, const string& camera_id, system_clock::time_point start_time, system_clock::time_point end_time)
{
    auto con = query_get_contents(top_dir, devices, camera_id, start_time, end_time);

    if(con.segments.empty())
        R_THROW(("No segments found for playback restream mount."));

    milliseconds playback_duration{0};
    for(auto& s : con.segments)
        playback_duration += duration_cast<milliseconds>(s.end - s.start);

    return make_tuple(con, playback_duration);
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

void _need_playback_data_cbs(GstElement* appsrc, guint, playback_restreaming_state* prs)
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

bool _time_to_get_more_data(playback_restreaming_state* prs)
{
    if(!prs->running) return false;

    // Prioritize video queue - fetch more aggressively to prevent visible stuttering
    // Increased threshold from 40 to 60 to fetch sooner and maintain larger buffer
    if(prs->video_samples.size() < 60) return true;

    // For audio, only fetch if video buffer is also getting low
    // This prevents too-frequent fetches triggered by audio-only consumption
    if(prs->has_audio && prs->audio_samples.size() < 60 && prs->video_samples.size() < 80)
        return true;

    return false;
}

// The session's storage reader. It holds no reference to prs: the media owns
// it, and joins this thread before deleting it (_playback_restream_cleanup).
// When this thread held one, it could hold the last, and the state's own
// destructor then ran here, on the thread it owned: std::terminate.
void _playback_entry_point(playback_restreaming_state* prs)
{
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

                        if(ts < prs->min_ts)
                            continue;

                        if(prs->first_ts.is_null())
                            prs->first_ts.set_value(ts);

                        if(system_clock::time_point(milliseconds(ts)) > prs->end_time)
                        {
                            prs->running = false;
                            break;
                        }

                        // Skip the chunk-overlap region: this fetch began at the
                        // keyframe before query_start, and everything up to the
                        // previous chunk's last frame was already posted.
                        if(sid == R_STORAGE_MEDIA_TYPE_VIDEO && ts <= prs->last_posted_v_ts)
                            continue;
                        if(sid == R_STORAGE_MEDIA_TYPE_AUDIO && ts <= prs->last_posted_a_ts)
                            continue;

                        r_gst_buffer buffer(frame.data(), frame.size());

                        _frame_context fc;
                        fc.gst_pts = (ts - prs->first_ts.value()) * 1000000;
                        fc.gst_dts = (ts - prs->first_ts.value()) * 1000000;
                        fc.key = key;
                        fc.buffer = buffer;

                        if(sid == R_STORAGE_MEDIA_TYPE_VIDEO)
                        {
                            prs->video_samples.post(fc);
                            prs->last_posted_v_ts = ts;
                        }
                        else if(sid == R_STORAGE_MEDIA_TYPE_AUDIO)
                        {
                            prs->audio_samples.post(fc);
                            prs->last_posted_a_ts = ts;
                        }
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

// The media's cleanup (its "rtsp-extra-data" destroy notify): stop and join the
// reader, then free the session.
void _playback_restream_cleanup(gpointer data)
{
    auto prs = static_cast<playback_restreaming_state*>(data);

    prs->running = false;
    if(prs->playback_thread.joinable())
        prs->playback_thread.join();

    // Release the appsrc refs taken via gst_bin_get_by_name_recurse_up in
    // _configure_session (previously leaked, one per session).
    if(prs->v_appsrc) { gst_object_unref(prs->v_appsrc); prs->v_appsrc = nullptr; }
    if(prs->a_appsrc) { gst_object_unref(prs->a_appsrc); prs->a_appsrc = nullptr; }

    delete prs;
}

void _configure_session(GstRTSPMedia* media, const playback_mount& pm)
{
    auto prs = new playback_restreaming_state(pm.top_dir, *pm.devices);
    prs->media = media;
    // The media owns the session from here: freed by _playback_restream_cleanup whatever happens below.
    g_object_set_data_full(G_OBJECT(media), "rtsp-extra-data", prs, _playback_restream_cleanup);

    tie(prs->friendly_name, prs->start_time, prs->end_time) =
        _get_playback_url_parts();

    prs->query_start = prs->start_time;
    prs->query_end = prs->start_time + seconds(5);
    prs->camera_id = pm.camera_id;

    tie(prs->con, prs->playback_duration) =
        _fetch_contents(pm.top_dir, *pm.devices, pm.camera_id, prs->start_time, prs->end_time);

    for(auto& s : prs->con.segments)
    {
        if(s.end >= prs->start_time)
        {
            prs->min_ts = r_time_utils::tp_to_epoch_millis(s.start);
            break;
        }
    }

    tie(prs->has_audio, prs->video_codec_name, prs->video_codec_parameters, prs->audio_codec_name, prs->audio_codec_parameters, prs->video_encoding, prs->maybe_audio_encoding) =
        _fetch_stream_info(prs, pm.camera_id);

    auto element = gst_rtsp_media_get_element(media);
    if(!element)
        R_THROW(("Failed to get element from media in restream media configure."));
    // gst_rtsp_media_get_element returns a new (transfer-full) ref; release it
    // on every exit path.
    std::shared_ptr<GstElement> element_guard(element, [](GstElement* e){ if(e) gst_object_unref(e); });

    prs->v_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "videosrc");
    if(!prs->v_appsrc)
        R_THROW(("Failed to get videosrc from element in restream media configure."));

    gst_util_set_object_arg(G_OBJECT(prs->v_appsrc), "format", "time");

    auto video_caps = _create_caps(prs->video_codec_parameters, "encoded_video_sample_caps");

    g_object_set(G_OBJECT(prs->v_appsrc), "caps", (GstCaps*)video_caps, NULL);

    g_object_set(G_OBJECT(prs->v_appsrc), "duration", chrono::duration_cast<chrono::nanoseconds>(prs->playback_duration).count(), NULL);

    g_signal_connect(prs->v_appsrc, "need-data", (GCallback)_need_playback_data_cbs, prs);

    prs->a_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "audiosrc");

    if(prs->a_appsrc)
    {
        gst_util_set_object_arg(G_OBJECT(prs->a_appsrc), "format", "time");

        auto audio_caps = _create_caps(prs->audio_codec_parameters, "encoded_audio_sample_caps");

        g_object_set(G_OBJECT(prs->a_appsrc), "caps", (GstCaps*)audio_caps, NULL);

        g_object_set(G_OBJECT(prs->a_appsrc), "duration", chrono::duration_cast<chrono::nanoseconds>(prs->playback_duration).count(), NULL);

        g_signal_connect(prs->a_appsrc, "need-data", (GCallback)_need_playback_data_cbs, prs);
    }

    // Running before the thread starts, so a cleanup that comes first stops it.
    prs->running = true;
    prs->playback_thread = std::thread(_playback_entry_point, prs);
}

void _playback_media_configure(GstRTSPMediaFactory*, GstRTSPMedia* media, playback_mount* pm)
{
    // A GLib signal handler: nothing may be thrown through it.
    try
    {
        _configure_session(media, *pm);
    }
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }
}

void _free_playback_mount(gpointer data, GClosure*)
{
    delete static_cast<playback_mount*>(data);
}

}

r_nullable<r_camera> r_vss::playback_camera(const vector<r_camera>& cameras, const string& name)
{
    r_nullable<r_camera> found;
    for(auto& c : cameras)
    {
        if(c.friendly_name.is_null())
            continue;
        // Normalize the camera's friendly name the same way URLs are created (friendly_mount_path, less its "/")
        // so we can match against the friendly_name extracted from the URL
        if(friendly_mount_path(c.friendly_name.value()).substr(1) == name)
        {
            found.set_value(c);
            break;
        }
    }
    return found;
}

bool r_vss::add_playback_mount(GstRTSPMountPoints* mounts, const string& url, const string& top_dir, r_devices& devices,
                               const string& camera_id, uint64_t start_ts, uint64_t end_ts)
{
    system_clock::time_point start_tp{milliseconds{start_ts}};
    system_clock::time_point end_tp{milliseconds{end_ts}};

    auto contents = query_get_contents(top_dir, devices, camera_id, start_tp, end_tp);

    if(contents.segments.empty())
    {
        R_LOG_ERROR("no segments found for playback restream mount: camera=%s, start_ts=%s, end_ts=%s", camera_id.c_str(), r_string_utils::uint64_to_s(start_ts).c_str(), r_string_utils::uint64_to_s(end_ts).c_str());
        return false;
    }

    auto first_segment = contents.segments.front();

    auto video_buffer = query_get_video(top_dir, devices, camera_id, first_segment.start, first_segment.start + seconds(5));

    uint32_t version = 0;
    auto bt = r_blob_tree::deserialize(video_buffer.data(), video_buffer.size(), version);

    if(!bt.has_key("has_audio"))
        R_THROW(("Blob tree missing audio indicator."));

    bool has_audio = (bt["has_audio"].get_string() == "true")?true:false;

    if(!bt.has_key("video_codec_name"))
        R_THROW(("Blob tree missing video codec name."));

    auto video_codec_name = bt["video_codec_name"].get_string();

    string audio_codec_name;
    if(has_audio)
    {
        if(!bt.has_key("audio_codec_name"))
            R_THROW(("Blob tree missing audio codec name but has audio!"));
        audio_codec_name = bt["audio_codec_name"].get_string();
    }

    auto video_encoding = r_pipeline::str_to_encoding(video_codec_name);

    r_nullable<r_pipeline::r_encoding> maybe_audio_encoding;
    if(has_audio && !audio_codec_name.empty())
    {
        try { maybe_audio_encoding.set_value(r_pipeline::str_to_encoding(audio_codec_name)); }
        catch(const std::exception& e) { R_LOG_WARNING("Unrecognized audio codec '%s' for playback mount, proceeding without audio: %s", audio_codec_name.c_str(), e.what()); }
    }

    auto launch_str = r_stream_keeper::create_restream_launch_string(
        video_encoding,
        r_pipeline::encoding_to_pt(video_encoding),
        maybe_audio_encoding,
        (maybe_audio_encoding.is_null())?0:r_pipeline::encoding_to_pt(maybe_audio_encoding.value())
    );

    auto factory = gst_rtsp_media_factory_new();
    if(!factory)
        R_THROW(("Failed to create restream media factory!"));

    gst_rtsp_media_factory_set_launch(factory, launch_str.c_str());

    gst_rtsp_media_factory_set_shared(factory, FALSE);

    r_rtsp_auth::allow_viewer(factory);

    // The mount owns what it plays (freed with its handler), so it never points at anything that can go before it.
    g_signal_connect_data(factory, "media-configure", (GCallback)_playback_media_configure,
                          new playback_mount{top_dir, &devices, camera_id}, _free_playback_mount, (GConnectFlags)0);

    gst_rtsp_mount_points_add_factory(mounts, url.c_str(), factory);
    return true;
}
