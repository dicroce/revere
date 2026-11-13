#include "r_vss/r_substream_recorder.h"
#include "r_vss/r_recording_context.h"
#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_live_restream_host.h"
#include "r_vss/r_rtsp_auth.h"
#include <gst/rtsp-server/rtsp-server.h>
#include "r_pipeline/r_arg.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include <algorithm>
#include <cmath>

using namespace r_vss;
using namespace r_disco;
using namespace r_pipeline;
using namespace r_storage;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

static int64_t _steady_ms()
{
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool r_vss::substream_snapshot_fits(int w, int h, int sub_w, int sub_h, int main_w, int main_h)
{
    if(w <= 0 || h <= 0 || sub_w <= 0 || sub_h <= 0 || main_w <= 0 || main_h <= 0)
        return false;
    const double sub_aspect = (double)sub_w / sub_h;
    const double main_aspect = (double)main_w / main_h;
    constexpr double ROUNDING = 1.04;
    if(fabs(sub_aspect - main_aspect) / main_aspect > ROUNDING - 1.0)
        return false;
    // main's picture in the box, shape kept
    const double out_w = std::min((double)w, h * main_aspect);
    const double out_h = std::min((double)h, w / main_aspect);
    return out_w <= sub_w * ROUNDING && out_h <= sub_h * ROUNDING;
}

string r_vss::substream_ring_path(const string& record_file_path)
{
    auto slash = record_file_path.find_last_of("/\\");
    auto dot = record_file_path.rfind('.');
    if(dot == string::npos || (slash != string::npos && dot < slash))
        return record_file_path + "_sub";
    return record_file_path.substr(0, dot) + "_sub" + record_file_path.substr(dot);
}

string r_vss::view_mount_path(const string& label, const string& main_mount, const string& sub_mount, bool sub_fresh, bool grid_tile)
{
    if(grid_tile && sub_fresh && !sub_mount.empty())
        return sub_mount;
    if(!main_mount.empty())
        return main_mount;
    string name = label;
    replace(begin(name), end(name), ' ', '_');
    return "/" + name;
}

bool r_vss::substream_in_use(const r_camera& camera, bool computer_on)
{
    return computer_on && r_disco::substream_on(camera) && !camera.sub_status.is_null() && camera.sub_status.value() == "ok";
}

bool r_vss::substream_wanted(const r_camera& camera, bool computer_on)
{
    return substream_in_use(camera, computer_on) && !camera.sub_rtsp_url.is_null() && !camera.record_file_path.is_null();
}

string r_vss::substream_mount_path(const r_camera& camera)
{
    // As the main mount is named (r_stream_keeper::add_restream_mount): the friendly name, spaces as underscores.
    string name = camera.friendly_name.is_null() ? camera.id : camera.friendly_name.value();
    replace(begin(name), end(name), ' ', '_');
    return "/" + name + "/sub";
}

bool r_vss::motion_on_sub_possible(const motion_source_inputs& in)
{
    if(!in.sub_analysis || !in.sub_in_use || !in.sub_fresh || !in.do_motion || !in.tier_1)
        return false;
    // The boxes motion finds are in the picture's letterbox: main's shape, or they mean something else on main.
    if(in.sub_w <= 0 || in.sub_h <= 0 || in.main_w <= 0 || in.main_h <= 0)
        return false;
    const double sub_aspect = (double)in.sub_w / in.sub_h;
    const double main_aspect = (double)in.main_w / in.main_h;
    if(fabs(sub_aspect - main_aspect) / main_aspect > 0.04)
        return false;
    // Key frames at least as often as main's, or detection would slow down; both known.
    if(in.sub_key_interval_ms < 0 || in.main_key_interval_ms < 0)
        return false;
    return in.sub_key_interval_ms <= in.main_key_interval_ms + MOTION_KEY_INTERVAL_SLACK_MS;
}

motion_source r_motion_source_governor::pass(const string& camera_id, const motion_source_inputs& in, int64_t now_ms)
{
    auto& st = _cameras[camera_id];
    const bool possible = motion_on_sub_possible(in);
    if(!possible)
    {
        st.possible = false;
        st.current = motion_source::main;       // at once
        return st.current;
    }
    if(!st.possible)
    {
        st.possible = true;
        st.possible_since_ms = now_ms;
    }
    if(st.current == motion_source::main && now_ms - st.possible_since_ms >= MOTION_SUB_STEADY_MS)
        st.current = motion_source::sub;
    return st.current;
}

void r_motion_source_governor::forget(const string& camera_id)
{
    _cameras.erase(camera_id);
}

substream_step r_vss::substream_next_step(bool wanted, bool main_receiving, bool running, bool running_dead, bool backoff_over)
{
    if(running)
        return (!wanted || !main_receiving || running_dead) ? substream_step::stop : substream_step::none;
    return (wanted && main_receiving && backoff_over) ? substream_step::start : substream_step::none;
}

void r_vss::ensure_substream_ring(const string& ring_path)
{
    if(!r_fs::file_exists(ring_path))
        r_storage_file::allocate(ring_path, SUBSTREAM_RING_BLOCK_SIZE, SUBSTREAM_RING_BLOCKS);
}

r_substream_recorder::r_substream_recorder(const r_camera& camera, const string& ring_path, const string& rtsp_protocols, r_frame_feeds* feeds, r_live_restream_host* sk, function<void(const r_live_key_frame&)> on_key_frame, motion_key_frame_cb on_motion_key_frame) :
    _camera(camera),
    _ring_path(ring_path),
    _feeds(feeds),
    _sk(sk),
    _fanout(make_unique<r_live_video_fanout>()),
    _mount_lock(),
    _mount_path(),
    _video_caps(),
    _on_key_frame(std::move(on_key_frame)),
    _ring(ring_path),
    _write_context(),
    _sdp_lock(),
    _sdp_medias(),
    _on_motion_key_frame(std::move(on_motion_key_frame)),
    _source(recording_source_name_prefix(camera) + "sub_")
{
    _last_video_ms = _steady_ms();

    vector<r_arg> arguments;
    add_argument(arguments, "url", _camera.sub_rtsp_url.value());
    // As main's recording (r_recording_context): the camera's timestamps as they are, and its transport.
    add_argument(arguments, "buffer-mode", string("0"));
    add_argument(arguments, "protocols", rtsp_protocols);
    if(!_camera.rtsp_username.is_null())
        add_argument(arguments, "username", _camera.rtsp_username.value());
    if(!_camera.rtsp_password.is_null())
        add_argument(arguments, "password", _camera.rtsp_password.value());
    _source.set_args(arguments);

    _source.set_sdp_media_cb([this](const map<string, r_sdp_media>& sdp_medias){
        lock_guard<mutex> g(_sdp_lock);
        _sdp_medias = sdp_medias;
        _answered = true;
    });

    // Video only: the substream's audio, if it has any, isn't wanted (live audio comes from main).
    _source.set_video_sample_cb([this](const sample_context& sc, const r_pipeline::r_gst_buffer& buffer, bool key, int64_t pts){
        try
        {
            // The smaller stream's live restream mount: video caps from the first frame, then the mount, and every
            // frame to its viewers before the ring write, as main's go (r_live_video_fanout).
            if(_sk)
            {
                if(_video_caps.is_null())
                {
                    _video_caps = _source.get_video_caps();
                    if(!_video_caps.is_null())
                    {
                        map<string, r_sdp_media> sdp_medias;
                        {
                            lock_guard<mutex> g(_sdp_lock);
                            sdp_medias = _sdp_medias;
                        }
                        try
                        {
                            auto path = _sk->add_substream_mount(_camera, sdp_medias, sc.video_encoding(), this);
                            lock_guard<mutex> g(_mount_lock);
                            _mount_path = path;
                        }
                        catch(const exception& e)
                        {
                            R_LOG_ERROR("substream of %s: no live restream mount: %s", _camera.id.c_str(), e.what());
                        }
                    }
                }
                _fanout->post(pts, key, buffer, [&](const function<void(live_restreaming_state&)>& fn) {
                    _sk->iterate_live_restreaming_states(_camera.id + "/sub", fn);
                });
            }

            string codec_name, codec_parameters;
            if(_write_context.is_null())
            {
                // The ring is written from the start of a GOP, so a reader never gets frames it can't decode.
                if(!key)
                    return;
                string sdp_parameters;
                int timebase;
                {
                    lock_guard<mutex> g(_sdp_lock);
                    tie(codec_name, sdp_parameters, timebase) = sdp_media_map_to_s(VIDEO_MEDIA, _sdp_medias);
                }
                codec_parameters = stored_video_codec_parameters(codec_name, sdp_parameters, _source, sc);
            }

            store_video_frame(buffer, key, sc.stream_start_ts() + pts, pts, codec_name, codec_parameters);
            ++_frames_written;
            _last_video_ms = _steady_ms();
        }
        catch(const exception& e)
        {
            R_LOG_ERROR("substream of %s: %s", _camera.id.c_str(), e.what());
        }
    });

    _source.play();
}

void r_substream_recorder::store_video_frame(const r_gst_buffer& buffer, bool key, int64_t ts, int64_t pts, const string& codec_name, const string& codec_parameters)
{
    if(_write_context.is_null())
    {
        _write_context = _ring.create_write_context(codec_name, codec_parameters, R_STORAGE_MEDIA_TYPE_VIDEO);
        _write_context->codec_name = codec_name;
        _write_context->codec_parameters = codec_parameters;
    }

    auto mi = buffer.map(r_pipeline::r_gst_buffer::MT_READ);
    // The time the ring stored the frame at: it differs from ts once storage has re-anchored a backward jump of the
    // camera's clock (or clamped a frame), as main's recording does. Everything that refers to the frame by time uses
    // it: the "sub" frame feed, "now" pictures and motion events, which are looked up in the recording by that time.
    const auto stored_ts = _ring.write_frame(_write_context.value(), R_STORAGE_MEDIA_TYPE_VIDEO, mi.data(), mi.size(), key, ts, pts);
    // Plugins' frame feeds on "sub": one atomic load while none is open.
    if(_feeds)
        _feeds->post_video(_camera.id, "sub", mi.data(), mi.size(), key, stored_ts, _write_context->codec_name, _write_context->codec_parameters);
    if(key)
    {
        _key_interval.record(_steady_ms());
        // Motion is found in key frames only: when the motion engine is fed from here, this is its frame.
        if(_feeds_motion && _on_motion_key_frame)
            _on_motion_key_frame(buffer, stored_ts, _write_context->codec_name, _write_context->codec_parameters);
        // The newest key frame, kept in memory for "now" snapshots.
        if(_on_key_frame)
        {
            r_live_key_frame kf;
            kf.ts = stored_ts;
            kf.codec_name = _write_context->codec_name;
            kf.codec_parameters = _write_context->codec_parameters;
            kf.data.assign(mi.data(), mi.data() + mi.size());
            _on_key_frame(kf);
        }
    }
}

string r_substream_recorder::mount_path() const
{
    lock_guard<mutex> g(_mount_lock);
    return _mount_path;
}

void r_substream_recorder::live_restream_media_configure(_GstRTSPMediaFactory*, _GstRTSPMedia* media)
{
    auto name = (_camera.friendly_name.is_null() ? _camera.id : _camera.friendly_name.value()) + " (smaller stream)";
    live_restream_configure_video_only(_sk, media, _camera.id + "/sub", name, _video_caps.value());
}

r_substream_recorder::~r_substream_recorder() noexcept
{
    try
    {
        _source.stop();
    }
    catch(...)
    {
    }
    // Its mount goes with it: a request for it then finds the main stream's, or nothing.
    try
    {
        if(_sk)
        {
            auto path = mount_path();
            if(!path.empty())
            {
                _sk->remove_restream_mount(path);
                _sk->remove_restream_mount(substream_id_mount_path(_camera.id));
            }
        }
    }
    catch(...)
    {
    }
    // Before the ring, which it writes to.
    _write_context.clear();
}

int64_t r_substream_recorder::_ms_since_video() const
{
    return _steady_ms() - _last_video_ms.load();
}

bool r_substream_recorder::fresh() const
{
    return _frames_written > 0 && _ms_since_video() <= SUBSTREAM_FRESH_MS;
}

bool r_substream_recorder::dead() const
{
    return _ms_since_video() > SUBSTREAM_DEAD_MS;
}

r_substream_decision r_substream_governor::pass(const string& camera_id, const r_substream_pass& in)
{
    auto& st = _cameras[camera_id];

    // A camera given up stays held off while the recording context whose copy of it still says "wanted" lives.
    bool wanted = in.wanted;
    if(st.given_up)
    {
        auto held = st.given_up_context.lock();
        if(held && held == in.context)
            wanted = false;
        else
            st.given_up = false;
    }

    const bool backoff_over = !st.started || in.now_ms - st.started_ms >= SUBSTREAM_RESTART_BACKOFF_MS;
    const bool dead = in.running && in.dead;

    r_substream_decision d;
    d.step = substream_next_step(wanted, in.main_receiving, in.running, dead, backoff_over);
    if(in.running && d.step == substream_step::none && in.ring_moved)
        d.step = substream_step::stop;

    if(d.step == substream_step::stop && dead)
    {
        // A recorder that never wrote a frame was refused (the camera may answer DESCRIBE and still refuse SETUP),
        // not merely lost.
        if(in.frames_written == 0)
        {
            if(++st.refusals >= SUBSTREAM_REFUSALS_TO_GIVE_UP)
            {
                d.gave_up = true;
                st.given_up = true;
                st.given_up_context = in.context;
                st.refusals = 0;
            }
        }
        else
            st.refusals = 0;
    }

    if(d.step == substream_step::start)
    {
        st.started = true;
        st.started_ms = in.now_ms;
    }

    // Main delivered video well past the window after the recorder started, with the recorder still up: it wasn't
    // cut by it, and earlier drops weren't the recorder's doing.
    if(in.running && !dead && st.started && in.now_ms - in.main_video_age_ms > st.started_ms + SUBSTREAM_MAIN_DROP_WINDOW_MS)
        st.main_drops = 0;
    return d;
}

bool r_substream_governor::main_dropped(const string& camera_id, int64_t now_ms, int64_t main_video_age_ms)
{
    auto it = _cameras.find(camera_id);
    if(it == _cameras.end() || !it->second.started)
        return false;
    auto& st = it->second;
    // When main's video stopped, against when the recorder started.
    const int64_t last_video_ms = now_ms - main_video_age_ms;
    if(last_video_ms < st.started_ms - SUBSTREAM_MAIN_DROP_SLACK_MS || last_video_ms >= st.started_ms + SUBSTREAM_MAIN_DROP_WINDOW_MS)
        return false;
    if(++st.main_drops < SUBSTREAM_REFUSALS_TO_GIVE_UP)
        return false;
    st.main_drops = 0;
    st.refusals = 0;
    return true;
}

void r_substream_governor::forget(const string& camera_id)
{
    _cameras.erase(camera_id);
}

static void _substream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media, gpointer user_data)
{
    ((r_substream_recorder*)user_data)->live_restream_media_configure((_GstRTSPMediaFactory*)factory, (_GstRTSPMedia*)media);
}

string r_vss::add_substream_mount_to(_GstRTSPMountPoints* mounts, const r_camera& camera, const map<string, r_sdp_media>& sdp_medias, r_nullable<r_encoding> video_encoding, r_substream_recorder* recorder)
{
    if(video_encoding.is_null())
        R_THROW(("No video encoding for the substream mount!"));
    if(sdp_medias.find("video") == sdp_medias.end())
        R_THROW(("No video stream found in SDP!"));
    auto& sdp_video = sdp_medias.at("video");
    if(sdp_video.formats.empty())
        R_THROW(("No video formats found in SDP!"));

    auto factory = gst_rtsp_media_factory_new();
    if(!factory)
        R_THROW(("Failed to create restream media factory!"));

    // Video only: the smaller stream carries none of the camera's audio, which is main's.
    auto launch_str = r_stream_keeper::create_restream_launch_string(video_encoding.value(), sdp_video.formats.front(), r_nullable<r_encoding>(), 0);
    gst_rtsp_media_factory_set_launch(factory, launch_str.c_str());
    gst_rtsp_media_factory_set_shared(factory, FALSE);
    r_rtsp_auth::allow_viewer(factory);
    g_signal_connect(factory, "media-configure", (GCallback)_substream_media_configure, recorder);

    auto mount_path = substream_mount_path(camera);
    add_factory_at_both(mounts, (GstRTSPMediaFactory*)factory, mount_path, substream_id_mount_path(camera.id));
    return mount_path;
}

string r_vss::restream_id_mount_path(const string& camera_id)
{
    return "/camera/" + camera_id;
}

string r_vss::substream_id_mount_path(const string& camera_id)
{
    return restream_id_mount_path(camera_id) + "/sub";
}

void r_vss::add_factory_at_both(_GstRTSPMountPoints* mounts, _GstRTSPMediaFactory* factory, const string& path, const string& id_path)
{
    // Each add takes a reference.
    g_object_ref(factory);
    gst_rtsp_mount_points_add_factory((GstRTSPMountPoints*)mounts, path.c_str(), (GstRTSPMediaFactory*)factory);
    gst_rtsp_mount_points_add_factory((GstRTSPMountPoints*)mounts, id_path.c_str(), (GstRTSPMediaFactory*)factory);
}
