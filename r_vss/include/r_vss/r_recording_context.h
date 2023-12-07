
#ifndef __r_vss_r_recording_context_h
#define __r_vss_r_recording_context_h

#include "r_vss/r_stream_keeper.h"
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

struct _GstRTSPMediaFactory;
typedef _GstRTSPMediaFactory GstRTSPMediaFactory;

struct _GstRTSPMedia;
typedef _GstRTSPMedia GstRTSPMedia;

namespace r_vss
{

class r_recording_context
{
public:
    R_API r_recording_context(r_stream_keeper* sk, const r_disco::r_camera& camera, const std::string& top_dir);
    R_API r_recording_context(const r_recording_context&) = delete;
    R_API r_recording_context(r_recording_context&&) = delete;
    R_API ~r_recording_context() noexcept;

    R_API r_recording_context& operator=(const r_recording_context&) = delete;
    R_API r_recording_context& operator=(r_recording_context&&) noexcept = delete;

    R_API bool dead() const;

    R_API r_disco::r_camera camera() const;

    R_API int32_t bytes_per_second() const;

    R_API void restream_media_configure(GstRTSPMediaFactory* factory, GstRTSPMedia* media);

    R_API void stop();

private:
    struct _frame_context
    {
        uint64_t gst_pts;
        uint64_t gst_dts;
        bool key;
        r_pipeline::r_gst_buffer buffer;
    };

    static void _need_data(GstElement* appsrc, guint unused, r_recording_context* rc);
    void need_data(GstElement* appsrc, guint unused);

    static void _seek_data(GstElement* appsrc, guint64 offset, r_recording_context* rc);
    void seek_data(GstElement* appsrc, guint64 offset);

    static void _restream_cleanup(r_recording_context* rc);

    void _final_storage_writer_audio_config(const r_pipeline::sample_context& sc);
    void _final_storage_writer_video_config(const r_pipeline::sample_context& sc);

    r_stream_keeper* _sk;
    r_disco::r_camera _camera;
    std::string _top_dir;
    r_pipeline::r_gst_source _source;
    r_storage::r_storage_file _storage_file;
    r_storage::r_storage_write_context _storage_write_context;
    std::chrono::system_clock::time_point _last_v_time;
    std::chrono::system_clock::time_point _last_a_time;
    bool _has_audio;
    std::chrono::system_clock::time_point _stream_start_ts;
    uint64_t _v_bytes_received;
    uint64_t _a_bytes_received;
    std::map<std::string, r_pipeline::r_sdp_media> _sdp_medias;
    r_utils::r_nullable<r_pipeline::r_gst_caps> _video_caps;
    r_utils::r_nullable<r_pipeline::r_gst_caps> _audio_caps;
    GstElement* _v_appsrc;
    GstElement* _a_appsrc;
    r_utils::r_blocking_q<_frame_context> _video_samples;
    r_utils::r_blocking_q<_frame_context> _audio_samples;
    bool _restreaming;
    bool _restream_key_sent;
    bool _first_restream_v_times_set;
    uint64_t _first_restream_v_pts;
    uint64_t _first_restream_v_dts;
    bool _first_restream_a_times_set;
    uint64_t _first_restream_a_pts;
    uint64_t _first_restream_a_dts;
    std::string _restream_mount_path;
    bool _got_first_audio_sample;
    bool _got_first_video_sample;
    bool _die;
};

}

#endif
