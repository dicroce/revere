
#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_motion_storage_sink.h"
#include "r_av/r_muxer.h"
#include "r_vss/r_recording_context.h"
#include "r_vss/r_query.h"
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

r_stream_keeper::r_stream_keeper(r_devices& devices, r_agent& agent, const string& top_dir) :
    _devices(devices),
    _agent(agent),
    _top_dir(top_dir),
    _th(),
    _running(false),
    _streams(),
    _cmd_q(),
    _rtsp_server_th(),
    _loop(g_main_loop_new(NULL, FALSE)),
    _server(gst_rtsp_server_new()),
    _mounts(nullptr),
    _factories(),
    _meph(_devices, _top_dir, *this),
    _motion_tee(_meph, [this](r_motion_event evt, const string& camera_id, int64_t ts) { _on_mog2_event(evt, camera_id, ts); }),
    _motionEngine(
        [this](const r_motion_work_item& item) -> std::shared_ptr<r_motion_work_context> {
            auto maybe_camera = _devices.get_camera_by_id(item.id);
            if(maybe_camera.is_null())
                R_THROW(("Motion engine unable to find camera with id: %s", item.id.c_str()));
            auto camera = maybe_camera.value();
            string path = camera.motion_detection_file_path.value();
            if(path.find('/') == string::npos && path.find('\\') == string::npos)
                path = _top_dir + PATH_SLASH + "video" + PATH_SLASH + path;
            r_motion_tuning tuning;
            if(_motion_tuning_fn)
                tuning = _motion_tuning_fn(camera.id);
            return make_shared<r_motion_work_context>(
                r_av::encoding_to_av_codec_id(item.video_codec_name),
                camera.id,
                r_pipeline::get_video_codec_extradata(item.video_codec_name, item.video_codec_parameters),
                make_unique<r_ring_storage_sink>(path, RING_MOTION_FLAG_SIZE),
                DEFAULT_MOTION_CONFIRM_FRAMES,
                tuning.min_motion_displacement,
                tuning.min_area_fraction
            );
        },
        _motion_tee
    ),
    _system_plugin_host(top_dir),
    _ws(top_dir, _devices, _agent, *this),
    _prune(_top_dir, _devices)
{
    _meph.register_detection_consumer(&r_stream_keeper::_on_detection_bus, this);

    auto video_path = top_dir + PATH_SLASH + "video";

    if(!r_fs::file_exists(video_path))
        r_fs::mkdir(video_path);

    gst_rtsp_server_set_address(_server, "0.0.0.0");

    gst_rtsp_server_set_service(_server, "8554");

    _mounts = gst_rtsp_server_get_mount_points(_server);

    _motionEngine.start();
    _system_plugin_host.connect_detection_bus(_meph);
    _system_plugin_host.start_all();
}

r_stream_keeper::~r_stream_keeper() noexcept
{
    if(_running)
        stop();

    _system_plugin_host.stop_all();
    _motionEngine.stop();

    // Clear the streams map BEFORE destroying server objects
    // This ensures recording contexts are destroyed while server is still valid
    {
        std::lock_guard<std::mutex> lock(_streams_mutex);
        _streams.clear();
    }

    for(auto f : _factories)
        g_object_unref(f);

    if(_mounts)
    {
        g_object_unref(_mounts);
        _mounts = nullptr;
    }

    if(_server)
    {
        g_object_unref(_server);
        _server = nullptr;
    }

    if(_loop)
        g_main_loop_unref(_loop);
}

void r_stream_keeper::set_motion_tuning_fn(r_motion_tuning_fn fn)
{
    _motion_tuning_fn = std::move(fn);
}

void r_stream_keeper::set_system_plugin_api_result_cb(std::function<void(const std::string&, const std::string&)> fn)
{
    _system_plugin_host.set_api_result_fn(std::move(fn));
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

    if(_meph.has_object_detector())
    {
        auto pass = make_unique<r_detection_pass>(
            [this](const string& camera_id, int64_t) -> r_nullable<r_letterboxed_frame> {
                std::shared_ptr<r_recording_context> rc;
                {
                    std::lock_guard<std::mutex> lock(_streams_mutex);
                    auto it = _streams.find(camera_id);
                    if(it != _streams.end())
                        rc = it->second;
                }
                r_nullable<r_letterboxed_frame> out;
                if(!rc)
                    return out;
                auto kf = rc->latest_key_frame();
                if(kf.is_null())
                    return out;
                auto frame = decode_letterboxed_key_frame(kf.value().codec_name, kf.value().codec_parameters, kf.value().data);
                frame.ts = kf.value().ts;
                out.set_value(std::move(frame));
                return out;
            },
            [this](const string& camera_id, int64_t ts) -> r_nullable<r_letterboxed_frame> {
                r_nullable<r_letterboxed_frame> out;
                out.set_value(query_get_letterboxed_key_frame(_top_dir, _devices, camera_id, r_time_utils::epoch_millis_to_tp(ts)));
                return out;
            },
            [this](const string& camera_id, const r_letterboxed_frame& frame, vector<r_detection>& out) {
                return _meph.detect_objects(camera_id, frame.ts, frame.rgb, frame.size, frame.size, out);
            },
            [this](const string& camera_id) -> string { return _camera_name(camera_id); },
            [this](const string& camera_id, const vector<r_detection>& detections) { _pass_found(camera_id, detections); },
            [this](const r_detection_session& session) { _pass_session(session); });
        pass->start();
        std::lock_guard<std::mutex> g(_detection_pass_lock);
        _detection_pass = std::move(pass);
    }

    _th = thread(&r_stream_keeper::_entry_point, this);

    _prune.start();
}

void r_stream_keeper::stop()
{
    if(!_running)
        R_THROW(("Cannot stop stream keeper if its not running!"));

    R_LOG_INFO("Stopping prune...");
    _prune.stop();
    R_LOG_INFO("Prune stopped");

    // Stop system plugins BEFORE the web server. The cloud plugin's export
    // thread makes loopback HTTP requests to the web server (e.g. /transcode);
    // if the web server is torn down first, an in-flight request has nothing to
    // answer it and blocks for the full media read timeout (120s), wedging
    // shutdown. Stopping the plugins first lets that export drain (or bail on
    // its _running flag) while the server is still up.
    R_LOG_INFO("Stopping system plugins...");
    _system_plugin_host.stop_all();
    R_LOG_INFO("System plugins stopped");

    // Now stop the web server so no new HTTP requests are handled.
    R_LOG_INFO("Stopping web server...");
    _ws.stop();
    R_LOG_INFO("Web server stopped");

    // Before the plugins it calls for detection.
    {
        std::lock_guard<std::mutex> g(_detection_pass_lock);
        if(_detection_pass)
        {
            R_LOG_INFO("Stopping detection pass...");
            _detection_pass->stop();
            R_LOG_INFO("Detection pass stopped");
        }
    }

    {
        std::lock_guard<std::mutex> lock(_streams_mutex);
        for(auto& s : _streams)
            s.second->stop();
    }

    R_LOG_INFO("Stopping motion engine...");
    _motionEngine.stop();
    R_LOG_INFO("Motion engine stopped");

    // Stop motion event plugins AFTER motion engine (no more events will be posted)
    // but BEFORE we quit the main loop (plugins may need GStreamer resources)
    R_LOG_INFO("Stopping motion event plugins...");
    _meph.stop();
    R_LOG_INFO("Motion event plugins stopped");

    _running = false;

    R_LOG_INFO("Quitting main loop...");
    g_main_loop_quit(_loop);
    R_LOG_INFO("Main loop quit");

    R_LOG_INFO("Joining stream keeper thread...");
    _th.join();
    R_LOG_INFO("Stream keeper thread joined");

    R_LOG_INFO("Joining rtsp server thread...");
    _rtsp_server_th.join();
    R_LOG_INFO("Rtsp server thread joined");

    R_LOG_INFO("Stopping system plugins...");
    _system_plugin_host.stop_all();
    R_LOG_INFO("System plugins stopped");
}

vector<r_stream_status> r_stream_keeper::fetch_stream_status()
{
    std::lock_guard<std::mutex> lock(_status_cache_mutex);
    return _status_cache;
}

void r_stream_keeper::_update_status_cache()
{
    // Fetch stream status with proper locking (thread safety fix)
    std::vector<r_stream_status> status;
    {
        std::lock_guard<std::mutex> lock(_streams_mutex);
        status = _fetch_stream_status();
    }

    // Check for motion engine overflow
    size_t motion_dropped = _motionEngine.get_and_reset_dropped_count();
    if(motion_dropped > 0)
    {
        _total_motion_dropped += motion_dropped;
        _current_overflow_flags |= r_overflow_type::motion_detection;
    }

    // Check for live restreaming overflow
    size_t restream_dropped = 0;
    {
        lock_guard<mutex> g(_live_restreaming_states_lok);
        for(auto& lrs_pair : _live_restreaming_states)
        {
            auto& lrs = lrs_pair.second;
            auto v = lrs->video_samples.dropped_count();
            auto a = lrs->audio_samples.dropped_count();
            lrs->video_samples.reset_dropped_count();
            lrs->audio_samples.reset_dropped_count();
            restream_dropped += v + a;
            if(v > 0 || a > 0)
            {
                if(_restream_overflow_since_log.empty())
                    _restream_overflow_window_start = chrono::steady_clock::now();
                auto& o = _restream_overflow_since_log[lrs->session_id];
                o.camera = lrs->camera_name;
                o.session = lrs->session_id;
                o.video_dropped += v;
                o.audio_dropped += a;
            }
        }
    }
    if(restream_dropped > 0)
    {
        _total_restream_dropped += restream_dropped;
        _current_overflow_flags |= r_overflow_type::live_restream;
    }

    auto now = chrono::steady_clock::now();

    // Track when overflow last occurred
    if(motion_dropped > 0 || restream_dropped > 0)
    {
        _last_overflow_time = now;
    }

    // Clear overflow flags after 60 seconds of no overflow
    if(_current_overflow_flags != r_overflow_type::none &&
       chrono::duration_cast<chrono::seconds>(now - _last_overflow_time).count() >= 60)
    {
        _current_overflow_flags = r_overflow_type::none;
        _total_motion_dropped = 0;
        _total_restream_dropped = 0;
    }

    // Log overflow warnings periodically (max once per 30 seconds)
    if(_current_overflow_flags != r_overflow_type::none &&
       chrono::duration_cast<chrono::seconds>(now - _last_overflow_log_time).count() >= 30)
    {
        _last_overflow_log_time = now;

        if(has_overflow(_current_overflow_flags, r_overflow_type::motion_detection))
        {
            R_LOG_WARNING("Motion detection queue overflow: %zu frames dropped (queue size: %zu/%d). System may be under heavy load.",
                         _total_motion_dropped, _motionEngine.get_queue_size(), MOTION_ENGINE_MAX_QUEUE_SIZE);
        }
        if(!_restream_overflow_since_log.empty())
        {
            // Per session and track, so the log says which viewer of which
            // camera fell behind (or isn't reading a track) and by how much.
            vector<restream_overflow> sessions;
            for(const auto& o : _restream_overflow_since_log)
                sessions.push_back(o.second);
            auto line = format_restream_overflow(sessions, (int)chrono::duration_cast<chrono::seconds>(now - _restream_overflow_window_start).count());
            if(!line.empty())
                R_LOG_WARNING("%s", line.c_str());
            _restream_overflow_since_log.clear();
        }
    }

    // Add overflow info to the status
    for(auto& s : status)
    {
        s.overflow_flags = _current_overflow_flags;
        s.dropped_frames = _total_motion_dropped + _total_restream_dropped;
    }

    std::lock_guard<std::mutex> lock(_status_cache_mutex);
    _status_cache = std::move(status);
}

vector<r_cameras::r_camera_span> r_stream_keeper::camera_spans(const string& camera_id, int64_t since_ms)
{
    std::shared_ptr<r_recording_context> rc;
    {
        std::lock_guard<std::mutex> lock(_streams_mutex);
        auto it = _streams.find(camera_id);
        if(it != _streams.end())
            rc = it->second;
    }
    return rc ? rc->camera_spans(since_ms) : vector<r_cameras::r_camera_span>();
}

static int64_t _epoch_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void r_stream_keeper::camera_transition(const string& camera_id, const r_cameras::r_camera_transition& t)
{
    auto now = _epoch_ms();
    int tier;
    {
        std::lock_guard<std::mutex> g(_tier_lock);
        if(t.what == "motion")
            _tiers.camera_motion(camera_id, t.active, now);
        else
            _tiers.camera_class(camera_id, t.what, t.active, now);
        tier = _tiers.tier(camera_id);
    }

    // Tier 2: the camera's own classes are the result; no YOLO.
    if(tier == motion_tier_2)
    {
        if(t.what != "motion")
            _camera_class_output(camera_id, t, now);
        return;
    }

    // Only tier 3 runs the detection pass. Tier 1 has the YOLO motion plugin
    // on MOG2's events; running the pass there too would be YOLO twice.
    if(tier != motion_tier_3)
        return;

    std::lock_guard<std::mutex> g(_detection_pass_lock);
    if(!_detection_pass)
        return;
    if(t.what == "motion")
        _detection_pass->motion(camera_id, t.source, t.active);
    else if(t.active)
        _detection_pass->camera_class(camera_id, t.what);
}

int r_stream_keeper::motion_tier(const string& camera_id)
{
    std::lock_guard<std::mutex> g(_tier_lock);
    return _tiers.tier(camera_id);
}

r_tier_status r_stream_keeper::motion_tier_status(const string& camera_id) const
{
    std::lock_guard<std::mutex> g(_tier_lock);
    return _tiers.status(camera_id);
}

string r_stream_keeper::_camera_name(const string& camera_id)
{
    auto c = _devices.get_camera_by_id(camera_id);
    return (!c.is_null() && !c.value().friendly_name.is_null()) ? c.value().friendly_name.value() : camera_id;
}

bool r_stream_keeper::_motion_detection_on(const string& camera_id)
{
    auto c = _devices.get_camera_by_id(camera_id);
    return !c.is_null() && !c.value().do_motion_detection.is_null() && c.value().do_motion_detection.value();
}

void r_stream_keeper::_on_mog2_event(r_motion_event evt, const string& camera_id, int64_t ts)
{
    std::lock_guard<std::mutex> g(_tier_lock);
    if(evt == motion_event_start)
        _mog2_starts[camera_id] = ts;
    else if(evt == motion_event_end)
    {
        auto it = _mog2_starts.find(camera_id);
        if(it != _mog2_starts.end())
        {
            _tiers.mog2_event(camera_id, it->second, ts);
            _mog2_starts.erase(it);
        }
    }
}

void r_stream_keeper::_on_detection_bus(void* userdata, const r_detection& det)
{
    auto sk = static_cast<r_stream_keeper*>(userdata);
    std::lock_guard<std::mutex> g(sk->_tier_lock);
    sk->_tiers.yolo_detection(det.camera_id, det.class_name, det.ts);
}

void r_stream_keeper::_tier_tick()
{
    auto now = _epoch_ms();

    vector<pair<string, std::shared_ptr<r_recording_context>>> streams;
    {
        std::lock_guard<std::mutex> lock(_streams_mutex);
        for(auto& s : _streams)
            streams.push_back(s);
    }

    // Each camera's setting, saved verification and subscription state,
    // gathered before the lock.
    struct fact { string id; string setting; int subscription; int saved_verified; };  // subscription: -1 none, 0 down, 1 up
    vector<fact> facts;
    for(auto& s : streams)
    {
        fact f {s.first, string(), -1, 0};
        auto c = _devices.get_camera_by_id(s.first);
        if(!c.is_null() && !c.value().motion_tier.is_null())
            f.setting = c.value().motion_tier.value();
        if(!c.is_null() && !c.value().motion_tier_verified.is_null())
            f.saved_verified = c.value().motion_tier_verified.value();
        if(s.second->has_camera_events())
            f.subscription = s.second->camera_events_status().up ? 1 : 0;
        facts.push_back(f);
    }

    vector<pair<string, r_tier_status>> changed;
    vector<pair<string, int>> verified_changed;   // camera id -> verification to save
    {
        std::lock_guard<std::mutex> g(_tier_lock);
        for(auto& f : facts)
        {
            // A verification saved before this run, given back once.
            if(_tiers_restored.insert(f.id).second)
                _tiers.restore(f.id, f.saved_verified, now);
            _tiers.set_setting(f.id, f.setting);
            if(f.subscription >= 0)
                _tiers.subscription(f.id, f.subscription == 1, now);
        }
        for(auto& id : _tiers.tick(now))
            changed.push_back({id, _tiers.status(id)});
        for(auto& f : facts)
        {
            auto v = _tiers.verified_tier(f.id);
            if(v != f.saved_verified)
                verified_changed.push_back({f.id, v});
        }
    }

    // Keep verification across restarts.
    for(auto& v : verified_changed)
    {
        auto c = _devices.get_camera_by_id(v.first);
        if(c.is_null())
            continue;
        auto camera = c.value();
        if(v.second == 0)
            camera.motion_tier_verified.clear();
        else
            camera.motion_tier_verified.set_value(v.second);
        _devices.save_camera(camera);
    }

    for(auto& c : changed)
    {
        R_LOG_INFO("motion tier [%s]: tier %d: %s", _camera_name(c.first).c_str(), c.second.tier, c.second.reason.c_str());
        // Off tier 1: MOG2 stops getting this camera's frames; free its decoder.
        if(c.second.tier != motion_tier_1)
            _motionEngine.remove_work_context(c.first);
    }
}

void r_stream_keeper::_camera_class_output(const string& camera_id, const r_cameras::r_camera_transition& t, int64_t now_ms)
{
    auto yolo = camera_class_to_yolo(t.what);
    if(yolo.empty() || !_motion_detection_on(camera_id))
        return;

    auto key = camera_id + "|" + t.what;
    r_detection d;
    d.camera_id = camera_id;
    d.class_name = yolo;
    d.confidence = 1.0f;   // cameras give no confidence
    whole_frame(d);

    if(t.active)
    {
        {
            std::lock_guard<std::mutex> g(_tier_lock);
            if(_camera_class_starts.count(key))
                return;
            _camera_class_starts[key] = now_ms;
        }
        d.ts = now_ms;
        _meph.emit_detection(d);
        return;
    }

    int64_t start = 0;
    {
        std::lock_guard<std::mutex> g(_tier_lock);
        auto it = _camera_class_starts.find(key);
        if(it == _camera_class_starts.end())
            return;
        start = it->second;
        _camera_class_starts.erase(it);
    }
    d.ts = start;
    write_metadata(camera_id, "camera_events", analytics_json(start, now_ms, {d}, "camera"), start);
}

static bool _same_detection(const r_detection& a, const r_detection& b)
{
    return a.ts == b.ts && a.class_name == b.class_name && a.x1 == b.x1 && a.y1 == b.y1 && a.x2 == b.x2 && a.y2 == b.y2;
}

void r_stream_keeper::_pass_found(const string& camera_id, const vector<r_detection>& detections)
{
    if(motion_tier(camera_id) != motion_tier_3 || !_motion_detection_on(camera_id))
        return;

    vector<r_detection> reported;
    for(auto& d : detections)
        if(reported_class(d.class_name))
            reported.push_back(d);

    // On the bus at once: an alert shouldn't wait for the session to finish.
    for(auto& d : reported)
        _meph.emit_detection(d);

    std::lock_guard<std::mutex> g(_tier_lock);
    _emitted_first[camera_id] = reported;
}

void r_stream_keeper::_pass_session(const r_detection_session& session)
{
    vector<r_detection> already;
    {
        std::lock_guard<std::mutex> g(_tier_lock);
        auto it = _emitted_first.find(session.camera_id);
        if(it != _emitted_first.end())
        {
            already = it->second;
            _emitted_first.erase(it);
        }
    }

    // A person check for the tier controller: the pass's YOLO against the
    // camera's own person reports (how a tier 3 camera earns tier 2).
    {
        bool yolo_person = any_of(session.detections.begin(), session.detections.end(),
                                  [](const r_detection& d) { return d.class_name == "person"; });
        bool camera_person = find(session.camera_classes.begin(), session.camera_classes.end(), "person") != session.camera_classes.end();
        std::lock_guard<std::mutex> g(_tier_lock);
        _tiers.pass_session(session.camera_id, yolo_person, camera_person);
    }

    if(motion_tier(session.camera_id) != motion_tier_3 || !_motion_detection_on(session.camera_id))
        return;

    vector<r_detection> kept;
    for(auto& d : session.detections)
        if(reported_class(d.class_name))
            kept.push_back(d);
    if(kept.empty())
        return;

    write_metadata(session.camera_id, "detection_pass", analytics_json(session.start_ms, session.end_ms, kept, "detection_pass"), kept.front().ts);

    for(auto& d : kept)
    {
        bool sent = false;
        for(auto& a : already)
            sent = sent || _same_detection(a, d);
        if(!sent)
            _meph.emit_detection(d);
    }
}

vector<r_detection_session> r_stream_keeper::detection_sessions(const string& camera_id, int64_t since_ms)
{
    std::lock_guard<std::mutex> g(_detection_pass_lock);
    return _detection_pass ? _detection_pass->results(camera_id, since_ms) : vector<r_detection_session>();
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
    if(mount_path.empty() || mount_path[0] != '/')
        return;

    // Safety check: return early if objects are null or invalid
    if(!_mounts || !GST_IS_RTSP_MOUNT_POINTS(_mounts))
        return;

    if(!_server || !GST_IS_RTSP_SERVER(_server))
        return;

    gst_rtsp_mount_points_remove_factory(_mounts, mount_path.c_str());

    raii_ptr<GstRTSPSessionPool> pool(
        gst_rtsp_server_get_session_pool(_server),
        [](GstRTSPSessionPool* p){g_object_unref(p);}
    );

    if(!pool.get() || !GST_IS_RTSP_SESSION_POOL(pool.get()))
        return;

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

void r_stream_keeper::post_frame_to_motion_engine(r_pipeline::r_gst_buffer buffer, int64_t ts, const std::string& video_codec_name, const std::string& video_codec_params, const std::string& camera_id, bool is_key_frame)
{
    _motionEngine.post_frame(buffer, ts, video_codec_name, video_codec_params, camera_id, is_key_frame);
}

vector<uint8_t> r_stream_keeper::get_jpg(const string& camera_id, int64_t ts, uint16_t w, uint16_t h)
{
    return query_get_jpg(_top_dir, _devices, camera_id, r_time_utils::epoch_millis_to_tp(ts), w, h);
}

std::chrono::hours r_stream_keeper::get_retention_hours(const string& camera_id)
{
    std::lock_guard<std::mutex> lock(_retention_cache_mutex);
    auto it = _retention_cache.find(camera_id);
    if(it != _retention_cache.end())
        return it->second;
    return std::chrono::hours(0);
}

void r_stream_keeper::_update_retention_cache()
{
    auto now = std::chrono::steady_clock::now();

    // Update every 5 minutes, or immediately on first run (when _last_retention_update is epoch).
    // Retention is measured in days for a ring-buffer recording, so it doesn't change much
    // minute-to-minute, but 5 minutes keeps the UI responsive after a camera is added or
    // removed without spamming storage file reads on the hot path.
    if(_last_retention_update.time_since_epoch().count() != 0 &&
       (now - _last_retention_update) < std::chrono::minutes(5))
        return;

    _last_retention_update = now;

    // Build new cache from all current streams
    // First, get a snapshot of camera IDs under the lock
    std::vector<std::string> camera_ids;
    {
        std::lock_guard<std::mutex> lock(_streams_mutex);
        camera_ids.reserve(_streams.size());
        for(const auto& stream : _streams)
            camera_ids.push_back(stream.first);
    }

    // Now query retention without holding the lock (queries can be slow)
    std::map<std::string, std::chrono::hours> new_cache;
    for(const auto& camera_id : camera_ids)
    {
        try
        {
            auto retention = query_get_retention_hours(_top_dir, _devices, camera_id);
            new_cache[camera_id] = retention;
        }
        catch(const std::exception& e)
        {
            R_LOG_WARNING("Failed to get retention for camera %s: %s", camera_id.c_str(), e.what());
            new_cache[camera_id] = std::chrono::hours(0);
        }
    }

    // Swap in the new cache
    {
        std::lock_guard<std::mutex> lock(_retention_cache_mutex);
        _retention_cache = std::move(new_cache);
    }
}

r_devices& r_stream_keeper::get_devices()
{
    return _devices;
}

void r_stream_keeper::set_system_password(const std::string& password)
{
    _ws.set_system_password(password);
}

bool r_stream_keeper::system_password_set() const
{
    return _ws.system_password_set();
}

void r_stream_keeper::clear_system_password()
{
    _ws.clear_system_password();
}

void r_stream_keeper::write_metadata(const string& camera_id, const string& stream_tag, const string& json_data, int64_t timestamp_ms)
{
    std::shared_ptr<r_recording_context> rc;
    {
        std::lock_guard<std::mutex> lock(_streams_mutex);
        auto it = _streams.find(camera_id);
        if(it != _streams.end())
            rc = it->second;
    }

    if(rc)
    {
        rc->write_metadata(stream_tag, json_data, timestamp_ms);
    }
    else
    {
        R_LOG_ERROR("Camera %s not found for metadata write", camera_id.c_str());
    }
}

std::vector<std::string> r_stream_keeper::get_loaded_system_plugins() const
{
    return _system_plugin_host.get_loaded_plugins();
}

bool r_stream_keeper::is_system_plugin_enabled(const std::string& plugin_name) const
{
    return _system_plugin_host.is_plugin_enabled(plugin_name);
}

void r_stream_keeper::set_system_plugin_enabled(const std::string& plugin_name, bool enabled)
{
    _system_plugin_host.set_plugin_enabled(plugin_name, enabled);
}

std::string r_stream_keeper::get_system_plugin_version(const std::string& plugin_name) const
{
    return _system_plugin_host.get_plugin_version(plugin_name);
}

std::string r_stream_keeper::get_system_plugin_status(const std::string& plugin_name) const
{
    return _system_plugin_host.get_plugin_status(plugin_name);
}

std::string r_stream_keeper::get_system_plugin_status_message(const std::string& plugin_name) const
{
    return _system_plugin_host.get_plugin_status_message(plugin_name);
}

void r_stream_keeper::bounce(const std::string& camera_id)
{
    r_stream_keeper_cmd cmd;
    cmd.cmd = R_SK_STOP;
    cmd.id = camera_id;

    _cmd_q.post(cmd).get();
}

void r_stream_keeper::suspend(const std::string& camera_id)
{
    r_stream_keeper_cmd cmd;
    cmd.cmd = R_SK_SUSPEND;
    cmd.id = camera_id;

    _cmd_q.post(cmd).get();
}

void r_stream_keeper::resume(const std::string& camera_id)
{
    r_stream_keeper_cmd cmd;
    cmd.cmd = R_SK_RESUME;
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
            launch_str += r_string_utils::format("rtppcmupay name=pay1 pt=%d )", audio_format);
        else if(maybe_audio_encoding.value() == r_pipeline::r_encoding::PCMA_ENCODING)
            launch_str += r_string_utils::format("rtppcmapay name=pay1 pt=%d )", audio_format);
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
            // All _streams map modifications are protected by _streams_mutex.
            //
            // IMPORTANT: dead, modified and removed recording contexts are fully
            // destroyed BEFORE any are added, so when a stream is recreated the
            // old r_recording_context (and its nanots_writer) is gone before a new
            // one opens the same file path and stream tags. They are destroyed
            // outside the lock: a context whose camera has stopped answering can
            // take seconds to wind down (its event subscription finishing a
            // request), and nothing else should wait on _streams_mutex for that.
            {
                std::vector<std::shared_ptr<r_recording_context>> doomed;
                {
                    std::lock_guard<std::mutex> lock(_streams_mutex);

                    for(auto it = _streams.begin(); it != _streams.end();)
                    {
                        if(it->second->dead())
                        {
                            R_LOG_WARNING("Dead stream detected, removing: %s", it->first.c_str());
                            // Only an attempt over the transport the fallback
                            // chose counts: a camera set to UDP never uses it,
                            // and one bounced after a settings change starts over.
                            auto& transport = _rtsp_transports[it->first];
                            auto was = transport.protocols();
                            if(it->second->rtsp_protocols() == was &&
                               transport.attempt_ended(it->second->answered(), it->second->receiving_video()))
                                R_LOG_WARNING("Camera %s answered but sent no video over RTSP/%s; retrying over %s",
                                              it->first.c_str(), was.c_str(), transport.protocols().c_str());
                            // The next pass starts it again: a reconnect.
                            _reconnects.record(it->first);
                            doomed.push_back(it->second);
                            it = _streams.erase(it);
                        }
                        else
                            ++it;
                    }

                    if(!_streams.empty())
                    {
                        auto cameras = _get_current_cameras();

                        for(auto& rc : _remove_recording_contexts(_devices.get_modified_cameras(cameras)))
                            doomed.push_back(rc);
                        for(auto& rc : _remove_recording_contexts(_devices.get_assigned_cameras_removed(cameras)))
                            doomed.push_back(rc);
                    }
                }

                doomed.clear();

                std::lock_guard<std::mutex> lock(_streams_mutex);
                if(_streams.empty())
                    _add_recording_contexts(_devices.get_assigned_cameras());
                else
                    _add_recording_contexts(_devices.get_assigned_cameras_added(_get_current_cameras()));
            }

            _tier_tick();

            auto c = _cmd_q.poll(std::chrono::seconds(2));

            if(!c.is_null())
            {
                auto cmd = c.take();

                if(cmd.first.cmd == R_SK_IS_RECORDING)
                {
                    r_stream_keeper_result result;
                    {
                        std::lock_guard<std::mutex> lock(_streams_mutex);
                        result.is_recording = _streams.find(cmd.first.id) != _streams.end();
                    }
                    cmd.second.set_value(result);
                }
                else if(cmd.first.cmd == R_SK_STOP)
                {
                    r_stream_keeper_result result;

                    // Get the recording context reference while holding the lock
                    std::shared_ptr<r_recording_context> rc;
                    {
                        std::lock_guard<std::mutex> lock(_streams_mutex);
                        auto it = _streams.find(cmd.first.id);
                        if(it != _streams.end())
                            rc = it->second;
                        // A bounce follows a settings change: try TCP afresh
                        _rtsp_transports.erase(cmd.first.id);
                    }

                    // Stop the recording context WITHOUT holding _streams_mutex
                    // This prevents deadlock if stop() triggers callbacks that need the lock
                    if(rc)
                    {
                        rc->stop();
                        // Clean up motion engine work context for this camera
                        _motionEngine.remove_work_context(cmd.first.id);
                    }
                    else
                    {
                        R_LOG_ERROR("Attempt to stop recording for camera %s that is not being recorded!", cmd.first.id.c_str());
                    }

                    cmd.second.set_value(result);
                }
                else if(cmd.first.cmd == R_SK_SUSPEND)
                {
                    r_stream_keeper_result result;

                    std::shared_ptr<r_recording_context> rc;
                    {
                        std::lock_guard<std::mutex> lock(_streams_mutex);
                        auto it = _streams.find(cmd.first.id);
                        if(it != _streams.end())
                        {
                            rc = it->second;
                            _streams.erase(it);
                        }
                        _suspended_cameras.insert(cmd.first.id);
                    }

                    if(rc)
                    {
                        rc->stop();
                        _motionEngine.remove_work_context(cmd.first.id);
                        // Fully destroy the recording context here — before we ack the
                        // suspend — so its nanots write context flushes and finalizes
                        // the current segment against the SQLite DB while that DB still
                        // exists. _streams held the only owning ref (see
                        // _add_recording_contexts), so this reset runs the destructor
                        // now. Otherwise suspend() returns while ~r_recording_context is
                        // still pending on this thread, and a caller that immediately
                        // deletes the camera's storage files (Change Retention, or Move
                        // Storage "start fresh") races the destructor's
                        // "UPDATE segment_blocks" finalize, which aborts the process on a
                        // now-missing table.
                        rc.reset();
                    }

                    cmd.second.set_value(result);
                }
                else if(cmd.first.cmd == R_SK_RESUME)
                {
                    r_stream_keeper_result result;

                    {
                        std::lock_guard<std::mutex> lock(_streams_mutex);
                        _suspended_cameras.erase(cmd.first.id);
                    }

                    // A resume follows a storage repair (move / reset / re-create),
                    // so drop any failure backoff: retry on the next poll instead
                    // of waiting out up to 300s from the previous failed attempt.
                    _failed_cameras.erase(cmd.first.id);

                    cmd.second.set_value(result);
                }
                else if(cmd.first.cmd == R_SK_CREATE_PLAYBACK_MOUNT)
                {
                    r_stream_keeper_result result;
                    {
                        std::lock_guard<std::mutex> lock(_streams_mutex);
                        _create_playback_mount(cmd.first.friendly_name, cmd.first.url, cmd.first.start_ts, cmd.first.end_ts);
                    }
                    cmd.second.set_value(result);
                }
                else R_THROW(("Unknown command sent to stream keeper!"));
            }

            // Update caches for non-blocking reads from GUI thread
            _update_status_cache();
            _update_retention_cache();
        }
        catch(const std::exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
        }
    }
}

void r_stream_keeper::_rtsp_server_entry_point()
{
    GMainContext* context = g_main_context_new();
    g_main_context_push_thread_default(context);
    gst_rtsp_server_attach(_server, context);
    while(_running)
    {
        while (g_main_context_pending(context))
            g_main_context_iteration(context, false);

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
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
    auto now = std::chrono::steady_clock::now();
    for(const auto& camera : cameras)
    {
        if(_streams.count(camera.id) > 0 || _suspended_cameras.count(camera.id) > 0)
            continue;

        auto name = camera.friendly_name.is_null() ?
            (camera.camera_name.is_null() ? camera.id : camera.camera_name.value()) :
            camera.friendly_name.value();

        // r_agent may have raised an alert for this camera (e.g. background
        // re-interrogation discovered a codec/profile change it can't safely
        // auto-apply). Don't attempt to start the recording context — surface
        // the alert via the existing _failed_cameras → r_stream_status path so
        // the UI shows the message on the camera's card.
        auto alert_n = _devices.get_camera_alert(camera.id);
        if(!alert_n.is_null())
        {
            auto& fc = _failed_cameras[camera.id];
            fc.friendly_name = name;
            fc.last_error = alert_n.value();
            // Don't bump attempt_count for alert-driven skips — the camera
            // isn't "trying and failing", it's flagged for user attention.
            // Leave next_retry_time at "now" (not pushed into the future) so
            // that once r_agent clears the alert, we proceed to construction
            // immediately on the next iteration without a leftover cooldown.
            fc.next_retry_time = now;
            continue;
        }

        // If this camera previously failed, honor the backoff window before retrying.
        // Without this, the main loop spins and floods logs when an r_recording_context
        // constructor throws (e.g. legacy nanots v1 .nts file rejected by v2 lib).
        auto fc_it = _failed_cameras.find(camera.id);
        if(fc_it != _failed_cameras.end() && fc_it->second.next_retry_time > now)
            continue;

        // UDP when the camera is set to it; otherwise TCP, or UDP standing in
        // for TCP the camera refused (r_rtsp_transport)
        auto protocols = (!camera.rtsp_transport.is_null() && camera.rtsp_transport.value() == "UDP") ?
            string("UDP") : _rtsp_transports[camera.id].protocols();
        R_LOG_INFO("Starting camera stream: %s (%s) over RTSP/%s", name.c_str(), camera.id.c_str(), protocols.c_str());
        try
        {
            _streams[camera.id] = make_shared<r_recording_context>(this, camera, _top_dir, _ws, _sim_mttf_seconds, protocols);
            // Construction succeeded — clear any prior failure state for this camera.
            _failed_cameras.erase(camera.id);
        }
        catch(const std::exception& e)
        {
            auto& fc = _failed_cameras[camera.id];
            fc.attempt_count++;
            fc.friendly_name = name;
            fc.last_error = e.what();
            // Exponential backoff: 1, 2, 4, 8, ..., capped at 300s (5 min).
            int shift = std::min(8, fc.attempt_count - 1);
            int delay_seconds = std::min(300, 1 << shift);
            fc.next_retry_time = now + std::chrono::seconds(delay_seconds);
            R_LOG_ERROR(
                "Failed to start camera stream %s (%s): %s -- backing off %ds (attempt %d)",
                name.c_str(), camera.id.c_str(), e.what(), delay_seconds, fc.attempt_count);
        }
    }
}

vector<shared_ptr<r_recording_context>> r_stream_keeper::_remove_recording_contexts(const std::vector<r_disco::r_camera>& cameras)
{
    vector<shared_ptr<r_recording_context>> removed;
    for(const auto& camera : cameras)
    {
        // Always clear backoff entry so a remove+re-add resets the retry counter.
        _failed_cameras.erase(camera.id);

        if(_streams.count(camera.id) > 0)
        {
            auto name = camera.friendly_name.is_null() ?
                (camera.camera_name.is_null() ? camera.id : camera.camera_name.value()) :
                camera.friendly_name.value();
            R_LOG_INFO("Stopping camera stream: %s (%s)", name.c_str(), camera.id.c_str());
            // Stop the recording context and clean up motion engine
            _stop(camera.id);
            // Take the recording context out; the caller destroys it (closing
            // its file handles) once _streams_mutex is released.
            removed.push_back(_streams[camera.id]);
            _streams.erase(camera.id);
        }
    }
    return removed;
}

vector<r_stream_status> r_stream_keeper::_fetch_stream_status() const
{
    vector<r_stream_status> statuses;
    statuses.reserve(_streams.size() + _failed_cameras.size());

    transform(
        _streams.begin(),
        _streams.end(),
        back_inserter(statuses),
        [this](const auto& c){
            r_stream_status s;
            s.camera = c.second->camera();
            s.bytes_per_second = c.second->bytes_per_second();
            s.receiving_video = c.second->receiving_video();
            s.storage_write_lag_ms = c.second->storage_write_lag_ms();
            s.storage_timeline_lag_ms = c.second->storage_timeline_lag_ms();
            s.has_camera_events = c.second->has_camera_events();
            if(s.has_camera_events)
                s.camera_events = c.second->camera_events_status();
            s.motion_tier = motion_tier_status(c.first);
            s.rtsp_transport = c.second->rtsp_protocols();
            const bool set_udp = !s.camera.rtsp_transport.is_null() && s.camera.rtsp_transport.value() == "UDP";
            s.rtsp_transport_fallback = !set_udp && s.rtsp_transport == "UDP";
            return s;
        }
    );

    // Surface backoff-failed cameras to the UI so the sidebar can show a
    // meaningful error instead of "N/A". Only id + friendly_name are populated
    // on the embedded r_camera here — the UI matches by id and shows the
    // failure_message text.
    for(const auto& kv : _failed_cameras)
    {
        r_stream_status s;
        s.camera.id = kv.first;
        s.camera.friendly_name.set_value(kv.second.friendly_name);
        s.failed = true;
        s.failure_message = kv.second.last_error;
        s.failure_attempt_count = kv.second.attempt_count;
        statuses.push_back(std::move(s));
    }

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

    // Clean up motion engine work context for this camera
    _motionEngine.remove_work_context(camera_id);
}

void r_stream_keeper::_client_connected_cbs(GstRTSPServer*, GstRTSPClient* client, r_stream_keeper* sk)
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

    if(!r_utils::r_time_utils::is_valid_iso_8601(end_time_s))
        return;

    auto start_time_s = parts.back();
    parts.pop_back();

    if(!r_utils::r_time_utils::is_valid_iso_8601(start_time_s))
        return;

    string friendly_name = r_string_utils::join(parts, '_');

    // Now, we have the friendly name, start time, and end time. Ultimately we need to send
    // this request to the proper r_recording_context, but to do that we need to access _streams
    // but that all happens on the thread processing our command q. So, we'll post a command to
    // it to take it from here.

    try
    {
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
    catch(const std::exception& e)
    {
        R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
    }
}

void r_stream_keeper::_create_playback_mount(const string& friendly_name, const string& url, uint64_t start_ts, uint64_t end_ts)
{
    for(auto s : _streams)
    {
        // Normalize the camera's friendly name the same way URLs are created (spaces -> underscores)
        // so we can match against the friendly_name extracted from the URL
        string normalized_name = s.second->camera().friendly_name.value();
        replace(begin(normalized_name), end(normalized_name), ' ', '_');

        if(normalized_name == friendly_name)
        {
            s.second->create_playback_mount(_server, _mounts, url, start_ts, end_ts);
            return;
        }
    }
}

void r_stream_keeper::add_live_restreaming_state(GstRTSPMedia* media, std::shared_ptr<live_restreaming_state> lrs)
{
    lock_guard<mutex> g(_live_restreaming_states_lok);
    _live_restreaming_states[media] = lrs;
}

void r_stream_keeper::remove_live_restreaming_state(GstRTSPMedia* media)
{
    lock_guard<mutex> g(_live_restreaming_states_lok);
    _live_restreaming_states.erase(media);
}

void r_stream_keeper::iterate_live_restreaming_states(const std::string& camera_id, std::function<void(live_restreaming_state&)> fn)
{
    // Create a snapshot of matching states to avoid holding lock during callback
    // This prevents deadlock if fn() needs to acquire locks or perform blocking operations
    std::vector<std::shared_ptr<live_restreaming_state>> snapshot;
    {
        lock_guard<mutex> g(_live_restreaming_states_lok);
        for(auto& lrs : _live_restreaming_states)
        {
            if(lrs.second->camera_id == camera_id)
                snapshot.push_back(lrs.second);
        }
    }

    // Call fn() without holding any locks
    for(auto& lrs : snapshot)
        fn(*lrs);
}

size_t r_stream_keeper::get_motion_engine_dropped_count()
{
    return _motionEngine.get_and_reset_dropped_count();
}

size_t r_stream_keeper::get_motion_engine_queue_size() const
{
    return _motionEngine.get_queue_size();
}

r_overflow_type r_stream_keeper::get_current_overflow_flags() const
{
    return _current_overflow_flags;
}

size_t r_stream_keeper::get_total_dropped_frames() const
{
    return _total_motion_dropped + _total_restream_dropped;
}
