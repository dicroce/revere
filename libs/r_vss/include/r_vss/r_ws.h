
#ifndef __revere_ws_h
#define __revere_ws_h

#include "r_http/r_web_server.h"
#include "r_http/r_http_exception.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_macro.h"
#include "r_disco/r_devices.h"
#include "r_storage/r_storage_file.h"
#include "r_vss/r_query.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_audio_decoder.h"
#include "r_av/r_audio_encoder.h"
#include "r_utils/r_uuid.h"
#include <vector>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace r_vss
{

class r_ws final
{
    struct r_transcode_session
    {
        uint16_t output_width;
        uint16_t output_height;
        uint32_t bitrate;
        uint32_t framerate_num;
        uint32_t framerate_den;

        std::string output_codec_name;

        std::unique_ptr<r_av::r_video_decoder> decoder;
        std::unique_ptr<r_av::r_video_encoder> encoder;
        bool codec_initialized = false;
        std::string output_video_codec_params;

        // Audio transcoding state
        std::unique_ptr<r_av::r_audio_decoder> audio_decoder;
        std::unique_ptr<r_av::r_audio_encoder> audio_encoder;
        std::vector<std::vector<float>> audio_channel_buffers; // per-channel sample accumulation
        bool audio_initialized = false;
        int audio_channels = 0;
        int audio_sample_rate = 0;
        int64_t audio_pts = 0;
        std::string output_audio_codec_params;

        // Target output audio params (0 = use source values)
        int target_audio_sample_rate = 0;
        int target_audio_channels = 0;

        std::chrono::steady_clock::time_point last_used;
        int64_t encoder_frame_count = 0; // monotonic counter used as encoder PTS
    };

    static constexpr size_t MAX_TRANSCODE_SESSIONS = 5;

public:
    R_API r_ws(const std::string& top_dir, r_disco::r_devices& devices);
    R_API ~r_ws();

    R_API void stop();
    R_API const std::string& get_top_dir() const;
    R_API r_disco::r_devices& get_devices();

private:
    r_http::r_server_response _get_jpg(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                       r_utils::r_socket& conn,
                                       const r_http::r_server_request& request);

    r_http::r_server_response _get_webp(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                        r_utils::r_socket& conn,
                                        const r_http::r_server_request& request);

    r_http::r_server_response _get_key_frame(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                             r_utils::r_socket& conn,
                                             const r_http::r_server_request& request);

    r_http::r_server_response _get_contents(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                            r_utils::r_socket& conn,
                                            const r_http::r_server_request& request);

    r_http::r_server_response _get_cameras(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                           r_utils::r_socket& conn,
                                           const r_http::r_server_request& request);

    r_http::r_server_response _get_export(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                          r_utils::r_socket& conn,
                                          const r_http::r_server_request& request);

    r_http::r_server_response _get_motion_events(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                                 r_utils::r_socket& conn,
                                                 const r_http::r_server_request& request);

    r_http::r_server_response _get_analytics(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                             r_utils::r_socket& conn,
                                             const r_http::r_server_request& request);

    r_http::r_server_response _get_video(const r_http::r_web_server<r_utils::r_socket>& r_ws,
                                         r_utils::r_socket& conn,
                                         const r_http::r_server_request& request);

    r_http::r_server_response _get_create_transcode_stream(const r_http::r_web_server<r_utils::r_socket>& ws,
                                                           r_utils::r_socket& conn,
                                                           const r_http::r_server_request& request);

    r_http::r_server_response _get_transcode(const r_http::r_web_server<r_utils::r_socket>& ws,
                                             r_utils::r_socket& conn,
                                             const r_http::r_server_request& request);

    r_http::r_server_response _get_finalize_transcode_stream(const r_http::r_web_server<r_utils::r_socket>& ws,
                                                             r_utils::r_socket& conn,
                                                             const r_http::r_server_request& request);

    r_http::r_server_response _get_transcode_export(const r_http::r_web_server<r_utils::r_socket>& ws,
                                                    r_utils::r_socket& conn,
                                                    const r_http::r_server_request& request);

    r_http::r_server_response _post_mcp(const r_http::r_web_server<r_utils::r_socket>& ws,
                                        r_utils::r_socket& conn,
                                        const r_http::r_server_request& request);

    void _evict_oldest_transcode_session();

    std::string _top_dir;
    r_disco::r_devices& _devices;
    r_http::r_web_server<r_utils::r_socket> _server;
    std::map<std::string, r_transcode_session> _sessions;
    std::mutex _sessions_mutex;
};

}

#endif
