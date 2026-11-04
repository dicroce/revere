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

r_substream_recorder::r_substream_recorder(const r_camera& camera, const string& ring_path, const string& rtsp_protocols) :
    _camera(camera),
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
            _ring.write_frame(_write_context.value(), R_STORAGE_MEDIA_TYPE_VIDEO, mi.data(), mi.size(), key, sc.stream_start_ts() + pts, pts);
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
