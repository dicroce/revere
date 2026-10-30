
#ifndef r_disco_r_onvif_provider_h
#define r_disco_r_onvif_provider_h

#include "r_disco/r_stream_config.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include "r_onvif/r_onvif_session.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace r_disco
{

class r_agent;

// A PTZ action a client asked for. op picks the operation; the rest carry its
// parameters. Velocities are normalized to [-1,1] (0 = no motion on that axis).
struct r_ptz_command
{
    enum class op_t { move, stop, goto_preset, list_presets };
    op_t op {op_t::stop};
    float pan {0.f};
    float tilt {0.f};
    float zoom {0.f};
    std::string timeout;          // move: optional xs:duration ("PT1S"); empty = none
    std::string preset_token;     // goto_preset
    std::string profile_token;    // optional; empty = the camera's first media profile
};

// A PTZ failure with a short code a client can act on: "no_ptz" (the camera has
// no ONVIF PTZ service), "unreachable" (no ONVIF device service answers), or
// "camera_error" (the camera refused or failed the command).
class r_ptz_error : public std::runtime_error
{
public:
    r_ptz_error(const std::string& code, const std::string& what) : std::runtime_error(what), _code(code) {}
    const std::string& code() const { return _code; }
private:
    std::string _code;
};

class r_onvif_provider
{
public:
    R_API r_onvif_provider(const std::string& top_dir, r_agent* agent);
    R_API ~r_onvif_provider();

    // should_continue, if set, is polled during the (blocking, multi-interface)
    // ONVIF discovery sweep; returning false aborts it early so a shutting-down
    // caller isn't forced to wait it out. Empty = run to completion.
    R_API std::vector<r_stream_config> poll(const std::function<bool()>& should_continue = {});

    // onvif_port/onvif_protocol: where the camera's device service was last
    // found (r_camera::onvif_port); null when unknown. Checked, and found again
    // if stale (r_onvif::find_device_service).
    R_API std::vector<r_onvif::onvif_profile_info> get_camera_profiles(
        const std::string& ipv4,
        const std::string& xaddrs,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        r_utils::r_nullable<int> onvif_port = r_utils::r_nullable<int>(),
        r_utils::r_nullable<std::string> onvif_protocol = r_utils::r_nullable<std::string>()
    );

    // Perform a PTZ action against the camera's ONVIF PTZ service. Returns the
    // camera's presets (only for op == list_presets; empty otherwise). Throws
    // r_ptz_error if the camera has no PTZ service, can't be reached, or rejects
    // the command. The camera's ONVIF session (endpoint, PTZ service, profile and
    // the SOAP version and auth it settled on) is kept between commands, so a
    // move is one request to the camera; commands to one camera run one at a
    // time, and don't wait on camera interrogation. Safe to call from any thread.
    R_API std::vector<r_onvif::onvif_ptz_preset> ptz_control(
        const std::string& ipv4,
        const std::string& xaddrs,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        const r_ptz_command& cmd,
        r_utils::r_nullable<int> onvif_port = r_utils::r_nullable<int>(),
        r_utils::r_nullable<std::string> onvif_protocol = r_utils::r_nullable<std::string>()
    );

    R_API void interrogate_camera(
        r_stream_config& sc,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        const std::string& preferred_profile_token = "",
        bool force = false  // skip the "skip if already recording" guard; used by background re-interrogation
    );

    R_API r_utils::r_nullable<r_stream_config> interrogate_camera(
        const std::string& id,
        const std::string& camera_name,
        const std::string& ipv4,
        const std::string& xaddrs,
        const std::string& address,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        const std::string& preferred_profile_token = "",
        r_utils::r_nullable<int> onvif_port = r_utils::r_nullable<int>(),
        r_utils::r_nullable<std::string> onvif_protocol = r_utils::r_nullable<std::string>()
    );

    // Drop the cached interrogation result for an id so the next
    // interrogate_camera() call re-queries the camera. Used after detecting
    // a network change (IP/port) so we don't return a stale rtsp_url.
    R_API void invalidate_cache(const std::string& id);

private:
    std::vector<r_stream_config> _fetch_configs(const std::string& top_dir, const std::function<bool()>& should_continue);
    void _cache_check_expiration(const std::string& id);
    std::string _top_dir;
    r_agent* _agent;

    struct _r_onvif_provider_cache_entry
    {
        std::chrono::steady_clock::time_point created;
        r_stream_config config;
    };

    struct _r_onvif_cam_hints
    {
        r_onvif::soap_version soap_ver;
        r_onvif::auth_mode auth_mode;
    };

    std::map<std::string, _r_onvif_provider_cache_entry> _cache;
    std::map<std::string, _r_onvif_cam_hints> _negotiated_params;

    // One camera's PTZ session, kept between commands (ptz_control). lock
    // serializes that camera's commands; cam is null until the first command,
    // and again after a failure, so the next one starts over.
    struct _ptz_session
    {
        std::mutex lock;
        std::string credentials;
        std::unique_ptr<r_onvif::r_onvif_cam> cam;
        std::string ptz_service;
        std::string profile_token;
    };
    void _open_ptz_session(
        _ptz_session& session,
        const std::string& ipv4,
        const std::string& xaddrs,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        r_utils::r_nullable<int> onvif_port,
        r_utils::r_nullable<std::string> onvif_protocol
    );
    std::vector<r_onvif::onvif_ptz_preset> _run_ptz(_ptz_session& session, const r_ptz_command& cmd);
    std::mutex _ptz_sessions_lock;
    std::map<std::string, std::shared_ptr<_ptz_session>> _ptz_sessions;
};

}

#endif
