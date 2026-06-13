
#ifndef r_disco_r_agent_h
#define r_disco_r_agent_h

#include "r_disco/r_stream_config.h"
#include "r_disco/r_camera.h"
#include "r_utils/r_timer.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include "r_disco/providers/r_onvif_provider.h"
#include <thread>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <chrono>

namespace r_disco
{

// - All cameras come from discovery. Manually added cameras still come from a discovery
//   provider (so that all the cameras work the same way), just a special one that is only
//   for manually entered cameras.
//
// - discover onvif cameras
//   for each camera
//     populate stream config structs from RTSP DESCRIBE + onvif info
//     generate stream config hash
//       - camera id is GUID, current IP address is just another stream config attribute
//       - onvif may have a GUID somewhere but if it doesn't, it looks like you can read /proc/net/arp to get HW addresses of recent connections
//

typedef std::function<void(const std::vector<std::pair<r_stream_config, std::string>>&)> changed_streams_cb;
typedef std::function<std::pair<r_utils::r_nullable<std::string>, r_utils::r_nullable<std::string>>(const std::string&)> credential_cb;
typedef std::function<bool(const std::string&)> is_recording_cb;

// Look up an assigned camera by id, returning the persisted record (with
// rtsp_url, codec params, profile token, etc.). Returns null if the camera
// is not assigned/known.
typedef std::function<r_utils::r_nullable<r_camera>(const std::string&)> camera_lookup_cb;

// Persist an updated camera record (used after a successful background
// re-interrogation that produced a safely-applicable change like a new URL).
typedef std::function<void(const r_camera&)> save_camera_cb;

// Set or clear a per-camera UI alert. The empty string clears the alert.
// Used when a background re-interrogation surfaces a change that the user
// must resolve (camera reconfigured incompatibly, re-interrogation kept
// failing, etc.).
typedef std::function<void(const std::string& camera_id, const std::string& message)> camera_alert_cb;

// Return the currently-assigned (recording-eligible) cameras. Used by the ONVIF
// provider to drive unicast self-heal: it probes these cameras' last-known IPs
// (and, if any are still unseen, sweeps the local subnet) so a camera that
// changed IP and doesn't answer multicast Probe still gets re-discovered.
typedef std::function<std::vector<r_camera>()> assigned_cameras_cb;

class r_agent
{
    friend class r_onvif_provider;

public:
    R_API r_agent(const std::string& top_dir);
    R_API ~r_agent() noexcept;

    R_API void set_stream_change_cb(changed_streams_cb cb) {_changed_streams_cb = cb;}
    R_API void set_credential_cb(credential_cb cb) {_credential_cb = cb;}
    R_API void set_is_recording_cb(is_recording_cb cb) {_is_recording_cb = cb;}
    R_API void set_camera_lookup_cb(camera_lookup_cb cb) {_camera_lookup_cb = cb;}
    R_API void set_save_camera_cb(save_camera_cb cb) {_save_camera_cb = cb;}
    R_API void set_camera_alert_cb(camera_alert_cb cb) {_camera_alert_cb = cb;}
    R_API void set_assigned_cameras_cb(assigned_cameras_cb cb) {_assigned_cameras_cb = cb;}

    R_API void start();
    R_API void stop();

    R_API std::vector<r_onvif::onvif_profile_info> get_camera_profiles(
        const std::string& ipv4,
        const std::string& xaddrs,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password
    );

    R_API void interrogate_camera(
        const std::string& camera_name,
        const std::string& ipv4,
        const std::string& xaddrs,
        const std::string& address,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        const std::string& preferred_profile_token = ""
    );

    R_API void forget(const std::string& id);

private:
    std::pair<r_utils::r_nullable<std::string>, r_utils::r_nullable<std::string>> _get_credentials(const std::string& id);
    bool _is_recording(const std::string& id);
    void _entry_point();
    void _process_new_or_changed_streams_configs();

    // Spawns (if eligible and not already in flight) a detached worker that
    // re-interrogates the given assigned camera in the background and either
    // saves the refreshed config or sets a camera alert when the change is
    // not safe to auto-apply.
    void _maybe_schedule_reinterrogation(const r_stream_config& new_sc);
    void _do_reinterrogation(const r_stream_config& discovered_sc, r_camera stored);

    // Assigned cameras as reported by the host (empty if no callback wired).
    // Called by r_onvif_provider (friend) to drive unicast self-heal.
    std::vector<r_camera> _get_assigned_cameras();

    std::thread _th;
    bool _running;
    std::unique_ptr<r_onvif_provider> _onvif_provider;
    changed_streams_cb _changed_streams_cb;
    std::string _top_dir;
    r_utils::r_timer _timer;
    std::mutex _device_config_hashes_mutex;
    std::map<std::string, std::string> _device_config_hashes;
    credential_cb _credential_cb;
    is_recording_cb _is_recording_cb;
    camera_lookup_cb _camera_lookup_cb;
    save_camera_cb _save_camera_cb;
    camera_alert_cb _camera_alert_cb;
    assigned_cameras_cb _assigned_cameras_cb;

    // Per-camera state for the background re-interrogation worker.
    // `in_flight` blocks a second worker from spawning while one is running.
    // `next_attempt` honors exponential backoff on persistent failures.
    struct _reinterrogation_state
    {
        bool in_flight {false};
        int attempt_count {0};
        std::chrono::steady_clock::time_point next_attempt;
    };
    std::mutex _reinterrogation_mutex;
    std::map<std::string, _reinterrogation_state> _reinterrogation_state_by_id;
};

}

#endif
