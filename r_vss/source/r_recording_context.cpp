
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
#include "r_pipeline/r_arg.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_exception.h"
#include <vector>

using namespace r_vss;
using namespace r_disco;
using namespace r_pipeline;
using namespace r_storage;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

r_recording_context::r_recording_context(r_stream_keeper* sk, const r_camera& camera, const string& top_dir) :
    _sk(sk),
    _camera(camera),
    _top_dir(top_dir),
    _source(camera.friendly_name + "_"),
    _storage_file(top_dir + PATH_SLASH + "video" + PATH_SLASH + camera.record_file_path.value()),
    _storage_write_context(),
    _last_v_time(system_clock::now()),
    _last_a_time(system_clock::now()),
    _has_audio(false),
    _stream_start_ts(system_clock::now()),
    _v_bytes_received(0),
    _a_bytes_received(0),
    _sdp_medias(),
    _video_caps(),
    _audio_caps(),
    _v_appsrc(nullptr),
    _a_appsrc(nullptr),
    _video_samples(),
    _audio_samples(),
    _restreaming(false),
    _restream_key_sent(false),
    _first_restream_v_times_set(false),
    _first_restream_v_pts(0),
    _first_restream_v_dts(0),
    _first_restream_a_times_set(false),
    _first_restream_a_pts(0),
    _first_restream_a_dts(0),
    _restream_mount_path(),
    _got_first_audio_sample(false),
    _got_first_video_sample(false),
    _die(false)
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

//            if(this->_audio_caps.is_null())
//            {
//                R_LOG_ERROR("NULL audio caps!");
//                this->_audio_caps = _source.get_audio_caps();
//                R_LOG_ERROR("AUDIO CAPS=%s", this->_audio_caps.is_null()?"NULL":"NOT NULL");
//                if(!this->_video_caps.is_null())
//                    this->_restream_mount_path = this->_sk->add_restream_mount(_sdp_medias, _camera, this, sc.video_encoding(), sc.audio_encoding());
//            }

            auto ts = sc.stream_start_ts() + pts;
            this->_storage_file.write_frame(
                this->_storage_write_context,
                R_STORAGE_MEDIA_TYPE_AUDIO,
                mi.data(),
                mi.size(),
                key,
                ts,
                pts
            );

            // and copy it into our audio queue for restreaming.
            if(this->_restreaming)
            {
                if(this->_restream_key_sent)
                {
                    if(!this->_first_restream_a_times_set)
                    {
                        this->_first_restream_a_times_set = true;
                        this->_first_restream_a_pts = pts * 1000000;
                        this->_first_restream_a_dts = pts * 1000000;
                    }

                    _frame_context fc;
                    fc.gst_pts = (pts * 1000000) - this->_first_restream_a_pts;
                    fc.gst_dts = (pts * 1000000) - this->_first_restream_a_dts;

                    fc.key = key;
                    fc.buffer = buffer;
                    _audio_samples.post(fc);
                    R_LOG_ERROR("audio sample posted key=%s", key ? "true" : "false");
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
                this->_storage_write_context,
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
                    this->_storage_write_context.video_codec_name,
                    this->_storage_write_context.video_codec_parameters,
                    this->_camera.id
                );
            }

            if(this->_restreaming)
            {
                if(this->_restream_key_sent || key)
                {
                    this->_restream_key_sent = true;

                    if(!this->_first_restream_v_times_set)
                    {
                        this->_first_restream_v_times_set = true;
                        this->_first_restream_v_pts = pts * 1000000;
                        this->_first_restream_v_dts = pts * 1000000;
                    }

                    _frame_context fc;
                    fc.gst_pts = (pts*1000000) - this->_first_restream_v_pts;
                    fc.gst_dts = (pts*1000000) - this->_first_restream_v_dts;
                    fc.key = key;
                    fc.buffer = buffer;
                    _video_samples.post(fc);
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

        string video_codec_name, video_codec_parameters;
        int video_timebase;
        tie(video_codec_name, video_codec_parameters, video_timebase) = sdp_media_map_to_s(VIDEO_MEDIA, this->_sdp_medias);

        string audio_codec_name, audio_codec_parameters;
        int audio_timebase = 0;
        if(this->_has_audio)
            tie(audio_codec_name, audio_codec_parameters, audio_timebase) = sdp_media_map_to_s(AUDIO_MEDIA, this->_sdp_medias);

        this->_storage_write_context = this->_storage_file.create_write_context(video_codec_name, video_codec_parameters, audio_codec_name, audio_codec_parameters);
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

void r_recording_context::restream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media)
{
    R_LOG_ERROR("media configure");
    auto element = gst_rtsp_media_get_element(media);
    if(!element)
        R_THROW(("Failed to get element from media in restream media configure."));

    R_LOG_ERROR("media configure 2");
    // pay0 is the video payloader
    // pay1 is the audio payloader which might now actually be present

    // attach media cleanup callback to unset _restreaming flag
    g_object_set_data_full(G_OBJECT(media), "rtsp-extra-data", this, (GDestroyNotify)_restream_cleanup);

    // set restreaming flag (used in frame callbacks to determine if we should send the frame to the restreamer)
    _restreaming = true;

    R_LOG_ERROR("media configure 3");

    _v_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "videosrc");
    if(!_v_appsrc)
        R_THROW(("Failed to get videosrc from element in restream media configure."));

    R_LOG_ERROR("media configure 4");

    gst_util_set_object_arg(G_OBJECT(_v_appsrc), "format", "time");

    g_object_set(G_OBJECT(_v_appsrc), "caps", (GstCaps*)_video_caps.value(), NULL);

    g_signal_connect(_v_appsrc, "need-data", (GCallback)r_recording_context::_need_data, this);

    R_LOG_ERROR("media configure 5");

    auto videoPayloader = gst_bin_get_by_name_recurse_up(GST_BIN(element), "pay0");

    _a_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "audiosrc");

    R_LOG_ERROR("media configure 6");

    if(_a_appsrc)
    {
        gst_util_set_object_arg(G_OBJECT(_a_appsrc), "format", "time");

        R_LOG_ERROR("audio caps: %p", (GstCaps*)_audio_caps.value());

        g_object_set(G_OBJECT(_a_appsrc), "caps", (GstCaps*)_audio_caps.value(), NULL);

        g_signal_connect(_a_appsrc, "need-data", (GCallback)r_recording_context::_need_data, this);        
    }
    else R_LOG_ERROR("no audio appsrc");

    R_LOG_ERROR("media configure 7");
}

void r_recording_context::stop()
{
    _source.stop();

    this->_storage_file.finalize(this->_storage_write_context);
}

void r_recording_context::_need_data(GstElement* appsrc, guint unused, r_recording_context* rc)
{
    rc->need_data(appsrc, unused);
}

void r_recording_context::need_data(GstElement* appsrc, guint unused)
{
    if(appsrc == _v_appsrc)
    {
        auto sample = _video_samples.poll(chrono::milliseconds(3000));
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
        auto sample = _audio_samples.poll(chrono::milliseconds(3000));
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

void r_recording_context::_restream_cleanup(r_recording_context* rc)
{
    rc->_video_samples.clear();
    rc->_audio_samples.clear();
    rc->_restreaming = false;
    rc->_restream_key_sent = false;
    rc->_first_restream_v_times_set = false;
    rc->_first_restream_a_times_set = false;
    rc->_first_restream_v_pts = 0;
    rc->_first_restream_v_dts = 0;
    rc->_first_restream_a_pts = 0;
    rc->_first_restream_a_dts = 0;
}

void r_recording_context::_final_storage_writer_audio_config(const r_pipeline::sample_context& sc)
{
    if(!sc.audio_sample_rate().is_null())
        this->_storage_write_context.audio_codec_parameters += ", sc_audio_rate=" + r_string_utils::uint32_to_s(sc.audio_sample_rate().value());
    if(!sc.audio_channels().is_null())
        this->_storage_write_context.audio_codec_parameters += ", sc_audio_channels=" + r_string_utils::uint32_to_s(sc.audio_channels().value());
}

void r_recording_context::_final_storage_writer_video_config(const r_pipeline::sample_context& sc)
{
    if(!sc.framerate().is_null())
        this->_storage_write_context.video_codec_parameters += ", sc_framerate=" + r_string_utils::double_to_s(sc.framerate().value());

    if(this->_storage_write_context.video_codec_name == "h264")
    {
        //, sprop-parameter-sets=Z2QACqzZRifmwFqAgICgAAB9IAAXcAHiRLLA,aOvjyyLA
        auto sps = sc.sprop_sps().value();
        auto pps = sc.sprop_pps().value();

        this->_storage_write_context.video_codec_parameters += string(", sprop-parameter-sets=") + sps + "," + pps;
    }
    else if(this->_storage_write_context.video_codec_name == "h265")
    {
        //, sprop-vps=QAEMAf//AIAAAAMAAAMAAAMAAAMAALUCQA==, sprop-sps=QgEBAIAAAAMAAAMAAAMAAAMAAKACgIAtH+W1kkbQzkkktySqSfKSyA==, sprop-pps=RAHBpVgeSA==
        auto sps = sc.sprop_sps().value();
        auto pps = sc.sprop_pps().value();
        auto vps = sc.sprop_vps().value();

        this->_storage_write_context.video_codec_parameters += string(", sprop-vps=") + vps + ", sprop-sps=" + sps + ", sprop-pps=" + pps;
    }
}
