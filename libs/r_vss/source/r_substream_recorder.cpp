#include "r_vss/r_substream_recorder.h"
#include "r_vss/r_recording_context.h"
#include "r_pipeline/r_arg.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"

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

string r_vss::substream_ring_path(const string& record_file_path)
{
    auto slash = record_file_path.find_last_of("/\\");
    auto dot = record_file_path.rfind('.');
    if(dot == string::npos || (slash != string::npos && dot < slash))
        return record_file_path + "_sub";
    return record_file_path.substr(0, dot) + "_sub" + record_file_path.substr(dot);
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

r_substream_recorder::r_substream_recorder(const r_camera& camera, const string& ring_path, const string& rtsp_protocols, r_frame_feeds* feeds) :
    _camera(camera),
    _ring_path(ring_path),
    _feeds(feeds),
    _ring(ring_path),
    _write_context(),
    _sdp_lock(),
    _sdp_medias(),
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
            if(_write_context.is_null())
            {
                // The ring is written from the start of a GOP, so a reader never gets frames it can't decode.
                if(!key)
                    return;
                string codec_name, sdp_parameters;
                int timebase;
                {
                    lock_guard<mutex> g(_sdp_lock);
                    tie(codec_name, sdp_parameters, timebase) = sdp_media_map_to_s(VIDEO_MEDIA, _sdp_medias);
                }
                auto parameters = stored_video_codec_parameters(codec_name, sdp_parameters, _source, sc);
                _write_context = _ring.create_write_context(codec_name, parameters, R_STORAGE_MEDIA_TYPE_VIDEO);
                _write_context->codec_name = codec_name;
                _write_context->codec_parameters = parameters;
            }

            auto mi = buffer.map(r_pipeline::r_gst_buffer::MT_READ);
            auto stored_ts = sc.stream_start_ts() + pts;
            _ring.write_frame(_write_context.value(), R_STORAGE_MEDIA_TYPE_VIDEO, mi.data(), mi.size(), key, stored_ts, pts);
            // Plugins' frame feeds on "sub": one atomic load while none is open.
            if(_feeds)
                _feeds->post_video(_camera.id, "sub", mi.data(), mi.size(), key, stored_ts, _write_context->codec_name, _write_context->codec_parameters);
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

r_substream_recorder::~r_substream_recorder() noexcept
{
    try
    {
        _source.stop();
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
