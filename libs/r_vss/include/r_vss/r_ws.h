
#ifndef __revere_ws_h
#define __revere_ws_h

#include "r_http/r_web_server.h"
#include "r_http/r_http_exception.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_ssl_server_socket.h"
#include "r_vss/r_ws_auth.h"
#include "r_utils/r_macro.h"
#include "r_disco/r_devices.h"
#include "r_disco/r_agent.h"
#include "r_storage/r_storage_file.h"
#include "r_vss/r_query.h"
#include "r_vss/r_storage_ops.h"
#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_audio_decoder.h"
#include "r_av/r_audio_encoder.h"
#include "r_utils/r_uuid.h"
#include "r_utils/r_blocking_q.h"
#include "r_utils/r_secure_store.h"
#include "r_utils/r_credential_crypto.h"
#include <vector>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>

namespace r_vss
{

class r_stream_keeper;

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
        int64_t audio_first_ts = -1;  // abs ms timestamp of first audio input packet
        std::string output_audio_codec_params;

        // Target output audio params (0 = use source values)
        int target_audio_sample_rate = 0;
        int target_audio_channels = 0;

        // Guarded by _sessions_mutex (eviction reads it), not by mtx.
        std::chrono::steady_clock::time_point last_used;

        // Framerate resampling state (persists across chunked transcode calls)
        int64_t next_pts = -1;        // current position in encoder timebase units
        int64_t first_ts = -1;        // ms timestamp of first video frame (for relative PTS)
        std::shared_ptr<std::vector<uint8_t>> last_decoded_frame; // last YUV frame for duplication

        // Held for a whole /transcode of this session, so its decoder and encoder
        // see one window at a time. Other sessions don't wait on it.
        std::mutex mtx;
    };

    static constexpr size_t MAX_TRANSCODE_SESSIONS = 5;

    enum class export_type { standard, transcode };

    struct export_job
    {
        std::string id;
        export_type type = export_type::standard;
        std::string camera_id;
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        std::string file_name;
        std::string exports_path;
        // transcode-only
        uint16_t width = 0;
        uint16_t height = 0;
        uint32_t bitrate = 0;
        uint32_t framerate_num = 0;
        uint32_t framerate_den = 0;
        std::string codec;
        int percent_complete = 0;
    };

    struct export_progress
    {
        int percent_complete = 0;
        std::chrono::steady_clock::time_point completed_at{};
        std::string file_path;
        bool failed = false;   // the worker threw; percent_complete is still set to 100 so the entry is pruned
    };

public:
    R_API r_ws(const std::string& top_dir, r_disco::r_devices& devices, r_disco::r_agent& agent, r_stream_keeper& stream_keeper);
    R_API ~r_ws();

    R_API void stop();
    R_API const std::string& get_top_dir() const;
    R_API r_disco::r_devices& get_devices();

    // System-password administration, intended for the trusted local (desktop)
    // admin surface. Setting from here needs no current-password check — local
    // access is the trust boundary. Invalidates outstanding web tokens.
    R_API void set_system_password(const std::string& password);
    R_API bool system_password_set() const;
    // Remove the stored system password (web UI returns to first-run setup).
    R_API void clear_system_password();

    // The HTTPS listener's self-signed certificate: SHA-256 fingerprint as
    // "AB:CD:..." (what a remote vision pins and what the UI shows so a user can
    // compare), and the PEM itself (for a future "download certificate"). Empty
    // if TLS setup failed.
    R_API std::string get_tls_fingerprint() const;
    R_API std::string get_tls_cert_pem() const;
private:
    // Route table, registered on both the HTTP and HTTPS servers.
    template<class SOK_T>
    void _add_routes(r_http::r_web_server<SOK_T>& server);
    // True once either server has begun shutting down (they stop together).
    bool _stopping() const;

    // Session endpoints: /session turns a bearer token into the browser's
    // session cookie (for a web UI restoring a stored token); /logout ends the
    // session and clears the cookie.
    r_http::r_server_response _post_session(r_utils::r_socket_base& conn, const r_http::r_server_request& request);
    r_http::r_server_response _post_logout(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    // The bearer token or (for GET/HEAD) session cookie a request carries.
    static r_utils::r_nullable<std::string> _request_token(const r_http::r_server_request& request);
    // The client's address, or "" if it can't be read.
    static std::string _peer_ip(r_utils::r_socket_base& conn);
    // True when conn is the HTTPS listener's.
    static bool _is_tls(r_utils::r_socket_base& conn);
    // Mints the local API token and publishes it to <top_dir>/local_api_token.
    void _publish_local_token();
    // Loads <top_dir>/tls/server.{crt,key}, generating and persisting a
    // self-signed pair on first run.
    void _load_or_create_tls();
    r_http::r_server_response _get_jpg(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_webp(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_key_frame(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_contents(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_version(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_cameras(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_export(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_motion_events(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_camera_events(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_analytics(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_video(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_create_transcode_stream(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_transcode(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_transcode_fmp4(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    // /transcode_fmp4?passthrough=1 — remux-only variant (video is never
    // decoded/encoded; G.711 audio is transcoded to AAC unless audio=0).
    r_http::r_server_response _get_passthrough_fmp4(r_utils::r_socket_base& conn, std::map<std::string, std::string> args);

    r_http::r_server_response _get_finalize_transcode_stream(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_transcode_export(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_export_progress(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_export_download(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_mcp(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_camera_profiles(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_measure_camera(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_measure_progress(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_measure_result(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_configure_camera(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_remove_camera(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_update_camera_properties(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_forget_camera(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_add_rtsp_camera(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    // Move storage / Change retention (the desktop's Camera properties).
    r_http::r_server_response _get_storage_info(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_storage_move(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_storage_change_retention(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _get_storage_op(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_storage_op_cancel(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    // System plugins (the desktop's Plugins section).
    r_http::r_server_response _get_system_plugins(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_system_plugin_enabled(r_utils::r_socket_base& conn, const r_http::r_server_request& request);
    r_http::r_server_response _get_keep_awake(r_utils::r_socket_base& conn, const r_http::r_server_request& request);
    r_http::r_server_response _post_keep_awake(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    // Starts kind ("move", "reset" or "retention") on its own thread, or
    // returns false when one is already running.
    bool _start_storage_op(const std::string& kind, const r_disco::r_camera& camera, std::function<void(r_storage_op&)> work);

    void _measure_entry_point();
    void _measure(const std::string& job_id);

    r_http::r_server_response _get_auth_status(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_login(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    r_http::r_server_response _post_set_password(r_utils::r_socket_base& conn, const r_http::r_server_request& request);

    // Auth helpers for the camera-management ("Record") endpoints.
    std::string _password_path() const;
    bool _system_password_set() const;
    r_utils::r_nullable<std::string> _load_system_password();
    void _store_system_password(const std::string& password);
    bool _token_valid(const r_http::r_server_request& request);
    void _require_auth(const r_http::r_server_request& request);   // throws 401 if not authorized

    void _evict_oldest_transcode_session();

    void _export_entry_point();
    void _export(export_job& job);
    void _transcode_export(export_job& job);

    std::string _top_dir;
    r_disco::r_devices& _devices;
    r_disco::r_agent& _agent;
    r_stream_keeper& _stream_keeper;
    // Plain HTTP on loopback only: the desktop web UI (http://localhost is a
    // secure context, so no certificate warning), the in-process cloud plugin,
    // vision and local MCP clients. HTTPS carries the same routes for remote
    // clients (currently also loopback; opening it up is a later step).
    r_http::r_web_server<r_utils::r_socket> _http;
    r_http::r_web_server<r_utils::r_ssl_server_socket> _https;
    std::shared_ptr<r_utils::r_ssl_server_config> _tls;
    // Sessions are shared so a /transcode keeps its own alive while finalize or
    // eviction drops it from the map. _sessions_mutex guards only the map (and
    // last_used); it is never held across a transcode.
    std::map<std::string, std::shared_ptr<r_transcode_session>> _sessions;
    std::mutex _sessions_mutex;

    // Concurrency ceiling for the on-demand fMP4 playback transcode. Each window
    // spins up a software encoder; without a cap, several simultaneous viewers
    // (or one plus the recorder) could starve the box. Over-limit requests get a
    // 503 and the client retries.
    static constexpr int MAX_FMP4_TRANSCODES = 4;
    std::atomic<int> _fmp4_active {0};

    // Separate, higher ceiling for the remux-only passthrough mode: no codec is
    // involved, each request is a storage read plus an in-memory mux, so a grid
    // of live tiles can run many at once without touching the transcode budget.
    static constexpr int MAX_FMP4_PASSTHROUGH = 24;
    std::atomic<int> _fmp4_passthrough_active {0};

    std::thread _export_th;
    std::atomic<bool> _export_running {false};
    r_utils::r_blocking_q<export_job> _export_q;
    std::map<std::string, export_progress> _export_progress;
    std::mutex _export_progress_mutex;

    // Camera-measurement jobs (used by the "Record" web flow). Measuring a
    // camera's bitrate streams RTSP for ~15s, far too long to hold an HTTP
    // connection open, so it runs as a background job that the client polls —
    // mirroring the export flow above.
    struct measure_job
    {
        std::string camera_id;
        r_utils::r_nullable<std::string> username;
        r_utils::r_nullable<std::string> password;
        std::string profile_token;
    };

    struct measure_result
    {
        int percent_complete = 0;       // 0 until done, then 100
        bool failed = false;
        std::string error;
        int64_t byte_rate = 0;
        std::string video_codec;
        std::vector<uint8_t> jpeg;      // small snapshot, JPEG-encoded
        std::chrono::steady_clock::time_point completed_at{};
    };

    std::thread _measure_th;
    std::atomic<bool> _measure_running {false};
    r_utils::r_blocking_q<std::string> _measure_q;   // job ids
    std::map<std::string, measure_job> _measure_jobs;
    std::map<std::string, measure_result> _measure_results;

    // The web UI's storage operation (r_storage_ops.h): one at a time, on
    // _storage_th, which the client polls through /storage_op. _storage_mutex
    // guards starting one and the camera id and kind.
    std::thread _storage_th;
    r_storage_op _storage_op;
    std::string _storage_op_camera_id;
    std::string _storage_op_kind;
    std::mutex _storage_mutex;
    std::mutex _measure_mutex;

    // Auth state (r_ws_auth.h): session tokens from /login, the per-run local
    // token for same-user processes, and the per-address login throttle. The
    // system password itself is stored encrypted-at-rest with the OS-protected
    // master key (same scheme as camera credentials).
    std::vector<uint8_t> _master_key;
    r_ws_auth _auth;
};

}

#endif
