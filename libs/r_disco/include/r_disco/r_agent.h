
#ifndef r_disco_r_agent_h
#define r_disco_r_agent_h

#include "r_disco/r_stream_config.h"
#include "r_disco/r_camera.h"
#include "r_disco/r_camera_move.h"
#include "r_disco/r_substream.h"
#include "r_utils/r_timer.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include "r_disco/providers/r_onvif_provider.h"
#include <thread>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <atomic>
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
// Whether the camera's recording is receiving video from it right now (frames in the last few seconds).
typedef std::function<bool(const std::string&)> receiving_video_cb;

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

// Set (a move) or clear (null) an assigned camera's address change, held or
// followed (see r_camera_move.h). The host app keeps it for its UI and API.
typedef std::function<void(const std::string& camera_id, const r_utils::r_nullable<r_camera_move>& move)> camera_move_cb;

// r_agent::answer_camera_move's outcome.
enum class move_answer { ok, no_move, moved_again };

// The MAC address of the host at ipv4 (port: one to talk to first, so the
// neighbour table has it), or null. Replaceable for tests.
typedef std::function<r_utils::r_nullable<std::string>(const std::string& ipv4, int port)> mac_lookup;

// Now, in milliseconds since the epoch. Replaceable for tests.
typedef std::function<int64_t()> clock_ms;

class r_agent
{
    friend class r_onvif_provider;

public:
    R_API r_agent(const std::string& top_dir);
    R_API ~r_agent() noexcept;

    R_API void set_stream_change_cb(changed_streams_cb cb) {_changed_streams_cb = cb;}
    R_API void set_credential_cb(credential_cb cb) {_credential_cb = cb;}
    R_API void set_is_recording_cb(is_recording_cb cb) {_is_recording_cb = cb;}
    R_API void set_receiving_video_cb(receiving_video_cb cb) {_receiving_video_cb = cb;}
    R_API void set_camera_lookup_cb(camera_lookup_cb cb) {_camera_lookup_cb = cb;}
    R_API void set_save_camera_cb(save_camera_cb cb) {_save_camera_cb = cb;}
    R_API void set_camera_alert_cb(camera_alert_cb cb) {_camera_alert_cb = cb;}
    R_API void set_assigned_cameras_cb(assigned_cameras_cb cb) {_assigned_cameras_cb = cb;}
    R_API void set_camera_move_cb(camera_move_cb cb) {_camera_move_cb = cb;}
    R_API void set_mac_lookup(mac_lookup l) {_mac_lookup = l;}
    // Whether to follow, with no pause, a move to an address that can't be checked (no MAC stored, none readable
    // there) rather than hold it until a person confirms (r_camera_move.h's load_camera_move_policy). Off by default.
    // A different MAC at the new address is held whatever this says.
    R_API void set_follow_unverified_moves(bool follow) {_follow_unverified_moves = follow;}
    R_API bool follow_unverified_moves() const {return _follow_unverified_moves.load();}
    R_API void set_clock_ms(clock_ms c) {_clock_ms = c;}

    // Notes the MAC of each assigned camera that has none, at its stored address, as discovery does for a camera it
    // reports: one added and never changed would otherwise keep none. Only with evidence the camera itself is there
    // now: discovery saw its identity at that address in this pass (`seen_at`: id -> ipv4), or, for one added by its
    // RTSP URL, its recording is receiving video from that address (set_receiving_video_cb). Otherwise the device
    // that took over the address (a lapsed lease) would have its MAC saved as the camera's. Not where a move is held,
    // nor at an address followed without a MAC (load_unconfirmed_addresses); a camera whose MAC couldn't be read is
    // tried again after MAC_NOTE_RETRY_MS. Each discovery pass calls it.
    R_API void note_missing_macs(const std::map<std::string, std::string>& seen_at);
    static constexpr int64_t MAC_NOTE_RETRY_MS = 10 * 60 * 1000;

    // Discovery's new or changed cameras (and each config's hash), after a
    // poll. Each is written through the changed-streams callback, except an
    // assigned camera now found at another address: that is held as a move
    // (see r_camera_move.h) and not written. A camera found at its stored
    // address without a stored MAC gets its MAC filled in.
    R_API void apply_discovered_changes(const std::vector<std::pair<r_stream_config, std::string>>& changed);

    // Called after each poll (right after apply_discovered_changes): reads the
    // MAC at each held move's new address and follows the move when
    // decide_camera_move says so, else notes why it's held (hold_reason_for);
    // drops followed entries after a day.
    R_API void recheck_held_moves();

    // The user's answer to a held move: confirm writes the new address and
    // re-checks the camera there; dismiss keeps the current address, and the
    // same new address isn't raised again (a different one is). False if the
    // camera has no held move.
    R_API bool confirm_camera_move(const std::string& camera_id);
    R_API bool dismiss_camera_move(const std::string& camera_id);
    // Clears a followed move's notice (the user has seen it).
    R_API void clear_followed_move(const std::string& camera_id);

    // An answer from elsewhere (the local API, for the cloud) to the move the
    // person was shown, its new address `new_ipv4`. confirm follows a held move
    // (writes the new address, then re-checks the camera there in the
    // background); dismiss keeps the current address of a held move, or marks a
    // followed one seen. no_move: the camera has no such move (or confirm of a
    // followed one); moved_again: its move now has another new address.
    R_API move_answer answer_camera_move(const std::string& camera_id, const std::string& new_ipv4, bool confirm);

    // The moves now held or followed, by camera id.
    R_API std::map<std::string, r_camera_move> camera_moves() const;

    // Empty, or a warning that discovery replies may be blocked (r_discovery_health), for the UI's discovered list.
    R_API std::string discovery_warning() const { return _onvif_provider ? _onvif_provider->discovery_warning() : std::string(); }

    R_API void start();
    R_API void stop();

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

    // Perform a PTZ action against a camera's ONVIF PTZ service. For
    // cmd.op == list_presets the result carries presets and PTZ-axis
    // capabilities; the other ops return an empty result. Throws r_ptz_error.
    // One camera's commands run one at a time; they don't wait on interrogation
    // (r_onvif_provider::ptz_control).
    R_API r_ptz_result ptz_control(
        const std::string& ipv4,
        const std::string& xaddrs,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        const r_ptz_command& cmd,
        r_utils::r_nullable<int> onvif_port = r_utils::r_nullable<int>(),
        r_utils::r_nullable<std::string> onvif_protocol = r_utils::r_nullable<std::string>()
    );

    // Read or set a camera's picture flip (r_onvif_provider::imaging_control). Throws r_ptz_error.
    R_API r_onvif::onvif_video_flip imaging_control(
        const std::string& ipv4,
        const std::string& xaddrs,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        const r_imaging_command& cmd,
        r_utils::r_nullable<int> onvif_port = r_utils::r_nullable<int>(),
        r_utils::r_nullable<std::string> onvif_protocol = r_utils::r_nullable<std::string>()
    );

    // Set a camera's clock to this computer's (r_onvif_provider::sync_camera_time). Throws r_ptz_error.
    R_API r_onvif::onvif_time_sync sync_camera_time(
        const std::string& ipv4,
        const std::string& xaddrs,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        r_utils::r_nullable<int> onvif_port = r_utils::r_nullable<int>(),
        r_utils::r_nullable<std::string> onvif_protocol = r_utils::r_nullable<std::string>()
    );

    R_API void interrogate_camera(
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

    // The camera's main profile (its onvif_profile_token, else its largest) and the profiles that could be its
    // substream, best first (r_onvif::substream_candidates), for the settings to offer. Throws when the camera can't
    // be asked.
    R_API r_substream_candidate_list list_substream_candidates(
        const r_camera& camera,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password
    );

    // Looks for the camera's substream among its ONVIF profiles (r_disco::find_substream) and records the outcome on
    // `camera`: substream_set when found, substream_clear with the reason when not, including when the camera can't
    // be asked. Nothing is saved; the caller does that. main_width/main_height are main's picture size (see
    // main_picture_size), 0 when unknown (the profile's own size is used). With only_profile_token, that profile is
    // the only candidate. Blocks while it probes (up to SUBSTREAM_MAX_PROBES streams), so call it off the UI thread.
    R_API r_substream_choice find_camera_substream(
        r_camera& camera,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        uint16_t main_width,
        uint16_t main_height,
        const std::string& only_profile_token = ""
    );

    // Drops what the agent remembers of a camera that was removed, its unconfirmed address included.
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
    // Atomic so the discovery sweep's should_continue predicate reliably observes
    // stop() flipping this to false from another thread (and so the read isn't a
    // data race with that write).
    std::atomic<bool> _running;
    std::unique_ptr<r_onvif_provider> _onvif_provider;
    changed_streams_cb _changed_streams_cb;
    std::string _top_dir;
    r_utils::r_timer _timer;
    std::mutex _device_config_hashes_mutex;
    std::map<std::string, std::string> _device_config_hashes;
    // Serializes the on-demand ONVIF operations (get_camera_profiles /
    // interrogate_camera) which can now be driven concurrently by the desktop
    // "Record" wizard thread and the web server's measure-camera worker. These
    // write r_onvif_provider's _cache/_negotiated_params maps, which are
    // otherwise unguarded. The 60s discovery poll() does NOT touch those maps,
    // so it intentionally runs lock-free and is never stalled by this.
    std::mutex _interrogation_mutex;
    credential_cb _credential_cb;
    is_recording_cb _is_recording_cb;
    receiving_video_cb _receiving_video_cb;
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

    // Address changes for assigned cameras (see r_camera_move.h).
    struct _held_move
    {
        r_stream_config discovered;
        std::string hash;
        r_camera_move move;
    };
    camera_move_cb _camera_move_cb;
    mac_lookup _mac_lookup;
    clock_ms _clock_ms;
    int64_t _now() const;
    mutable std::mutex _moves_mutex;
    std::map<std::string, _held_move> _held_moves;
    std::map<std::string, r_camera_move> _followed_moves;
    std::atomic<bool> _follow_unverified_moves {false};
    // Cameras followed without pausing while no MAC was on file: id -> that address (load_unconfirmed_addresses).
    // Kept across restarts; under _moves_mutex.
    std::map<std::string, std::string> _unconfirmed_addresses;
    // When note_missing_macs last failed to read a camera's MAC (the agent's clock, ms); under _moves_mutex.
    std::map<std::string, int64_t> _mac_note_tries;
    // Notes a MAC-less camera's MAC at its stored address, unless that address is unconfirmed; true when it did.
    bool _note_mac(const std::string& id);
    // Records (ipv4) or ends (empty) a camera's unconfirmed address, and saves the list when it changed.
    void _set_unconfirmed_address(const std::string& id, const std::string& ipv4);
    // Held from a change to the list through its write, so two changes at once are written in the order they were
    // made and the file never ends with the older list. Taken before _moves_mutex, which the write doesn't hold.
    std::mutex _unconfirmed_save_mutex;
    // A dismissed move's new address, per camera: not raised again.
    std::map<std::string, std::string> _dismissed_moves;
    void _notify_move(const std::string& id, const r_utils::r_nullable<r_camera_move>& move);
    // A person confirmed the move: the camera's MAC becomes the one seen at the new address (none when it
    // can't be read there, so it's noted again at the confirmed address), then the move is followed.
    void _confirm(const std::string& id, _held_move held);
    void _follow(const std::string& id, _held_move held, const std::string& why);
};

}

#endif
