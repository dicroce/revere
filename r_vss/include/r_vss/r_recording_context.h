
#ifndef __r_vss_r_recording_context_h
#define __r_vss_r_recording_context_h

#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_ws.h"
#include "r_disco/r_camera.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_sample_context.h"
#include "r_pipeline/r_gst_buffer.h"
#include "r_pipeline/r_stream_info.h"
#include "r_storage/r_storage_file.h"
#include "r_utils/r_blocking_q.h"
#include "r_utils/r_macro.h"
#include <mutex>
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <thread>

struct _GstRTSPMediaFactory;
typedef _GstRTSPMediaFactory GstRTSPMediaFactory;

struct _GstRTSPMedia;
typedef _GstRTSPMedia GstRTSPMedia;

namespace r_vss
{

class r_recording_context;

struct _frame_context
{
    uint64_t gst_pts;
    uint64_t gst_dts;
    bool key;
    r_pipeline::r_gst_buffer buffer;
};

struct live_restreaming_state
{
    r_recording_context* rc {nullptr};
    GstRTSPMedia* media {nullptr};

    GstElement* v_appsrc {nullptr};
    GstElement* a_appsrc {nullptr};
    bool live_restream_key_sent {false};
    bool first_restream_v_times_set {false};
    uint64_t first_restream_v_pts {0};
    uint64_t first_restream_v_dts {0};
    bool first_restream_a_times_set {false};
    uint64_t first_restream_a_pts {0};
    uint64_t first_restream_a_dts {0};
    r_utils::r_blocking_q<struct _frame_context> video_samples;
    r_utils::r_blocking_q<struct _frame_context> audio_samples;
};

struct playback_restreaming_state
{
    playback_restreaming_state(r_ws& ws) : ws(ws) {}

    r_recording_context* rc {nullptr};
    GstRTSPMedia* media {nullptr};
    r_ws& ws;

    std::string friendly_name;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::chrono::system_clock::time_point query_start;
    std::chrono::system_clock::time_point query_end;
    std::string camera_id;
    r_utils::r_nullable<int64_t> first_ts;

    contents con;
    std::chrono::milliseconds playback_duration {0};

    bool has_audio {false};
    std::string video_codec_name;
    std::string video_codec_parameters;
    std::string audio_codec_name;
    std::string audio_codec_parameters;
    r_pipeline::r_encoding video_encoding;
    r_utils::r_nullable<r_pipeline::r_encoding> maybe_audio_encoding;
    r_utils::r_blocking_q<struct _frame_context> video_samples;
    r_utils::r_blocking_q<struct _frame_context> audio_samples;
    std::thread playback_thread;
    bool running;

    GstElement* v_appsrc {nullptr};
    GstElement* a_appsrc {nullptr};
};

class r_recording_context
{
public:
    R_API r_recording_context(r_stream_keeper* sk, const r_disco::r_camera& camera, const std::string& top_dir, r_ws& ws);
    R_API r_recording_context(const r_recording_context&) = delete;
    R_API r_recording_context(r_recording_context&&) = delete;
    R_API ~r_recording_context() noexcept;

    R_API r_recording_context& operator=(const r_recording_context&) = delete;
    R_API r_recording_context& operator=(r_recording_context&&) noexcept = delete;

    R_API bool dead() const;

    R_API r_disco::r_camera camera() const;

    R_API int32_t bytes_per_second() const;

    R_API void live_restream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media);

    R_API void stop();

    R_API void create_playback_mount(GstRTSPServer* server, GstRTSPMountPoints* mounts, const std::string& url, uint64_t start_ts, uint64_t end_ts);

private:
    std::tuple<std::string, std::chrono::system_clock::time_point, std::chrono::system_clock::time_point> _get_playback_url_parts();


    static void _live_restream_cleanup_cbs(live_restreaming_state* lrs);

    static void _playback_restream_media_configure_cbs(GstRTSPMediaFactory* factory, GstRTSPMedia* media, r_recording_context* rc);
    void _playback_restream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media);

    static void _playback_restream_cleanup_cbs(playback_restreaming_state* prs);

    // playback restreaming
    // TODO

    void _final_storage_writer_audio_config(const r_pipeline::sample_context& sc);
    void _final_storage_writer_video_config(const r_pipeline::sample_context& sc);
    r_storage::r_storage_write_context _create_storage_writer_context();

    r_stream_keeper* _sk;
    r_disco::r_camera _camera;
    std::string _top_dir;
    r_pipeline::r_gst_source _source;
    r_storage::r_storage_file _storage_file;
    r_utils::r_nullable<r_storage::r_storage_write_context> _maybe_storage_write_context;
    std::chrono::system_clock::time_point _last_v_time;
    std::chrono::system_clock::time_point _last_a_time;
    bool _has_audio;
    std::chrono::system_clock::time_point _stream_start_ts;
    uint64_t _v_bytes_received;
    uint64_t _a_bytes_received;
    std::map<std::string, r_pipeline::r_sdp_media> _sdp_medias;
    r_utils::r_nullable<r_pipeline::r_gst_caps> _video_caps;
    r_utils::r_nullable<r_pipeline::r_gst_caps> _audio_caps;
    std::string _restream_mount_path;
    std::mutex _live_restreaming_states_lok;
    std::map<GstRTSPMedia*, std::shared_ptr<live_restreaming_state>> _live_restreaming_states;
    std::mutex _playback_restreaming_states_lok;
    std::map<GstRTSPMedia*, std::shared_ptr<playback_restreaming_state>> _playback_restreaming_states;
    bool _got_first_audio_sample;
    bool _got_first_video_sample;
    bool _die;
    r_ws& _ws;
};

}

#endif
