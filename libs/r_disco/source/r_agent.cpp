
#include "r_disco/r_agent.h"
#include "r_onvif/r_onvif_session.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_md5.h"
#include <algorithm>
#include <iterator>
#include <vector>
#include <functional>
#include <thread>
#include <sstream>

using namespace r_disco;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

r_agent::r_agent(const std::string& top_dir) :
    _th(),
    _running(false),
    _onvif_provider(make_unique<r_onvif_provider>(top_dir, this)),
    _changed_streams_cb(),
    _top_dir(top_dir),
    _timer(),
    _device_config_hashes_mutex(),
    _device_config_hashes(),
    _credential_cb()
{
}

r_agent::~r_agent() noexcept
{
    stop();
}

void r_agent::start()
{
    _running = true;
    _th = std::thread(&r_agent::_entry_point, this);
}

void r_agent::stop()
{
    if(_running)
    {
        _running = false;
        _th.join();
    }
}

vector<r_onvif::onvif_profile_info> r_agent::get_camera_profiles(
    const string& ipv4,
    const string& xaddrs,
    r_nullable<string> username,
    r_nullable<string> password
)
{
    lock_guard<mutex> lk(_interrogation_mutex);
    return _onvif_provider->get_camera_profiles(ipv4, xaddrs, username, password);
}

void r_agent::interrogate_camera(
    const std::string& camera_name,
    const std::string& ipv4,
    const std::string& xaddrs,
    const std::string& address,
    r_nullable<string> username,
    r_nullable<string> password,
    const std::string& preferred_profile_token
)
{
    lock_guard<mutex> lk(_interrogation_mutex);

    r_md5 hash;
    hash.update((uint8_t*)address.c_str(), address.size());
    hash.finalize();
    auto id = hash.get_as_uuid();

    auto sc = _onvif_provider->interrogate_camera(id, camera_name, ipv4, xaddrs, address, username, password, preferred_profile_token);

    vector<pair<r_stream_config, string>> output;
    output.push_back(make_pair(sc.value(), hash_stream_config(sc.value())));

    if(_changed_streams_cb)
        _changed_streams_cb(output);
}

void r_agent::forget(const std::string& id)
{
    lock_guard<mutex> lock(_device_config_hashes_mutex);
    _device_config_hashes.erase(id);
}

pair<r_nullable<string>, r_nullable<string>> r_agent::_get_credentials(const std::string& id)
{
    if(!_credential_cb)
        R_THROW(("Please set a credential callback on r_agent before calling start."));

    return _credential_cb(id);
}

bool r_agent::_is_recording(const std::string& id)
{
    if(!_is_recording_cb)
        R_THROW(("Please set a is_recording callback on r_agent before calling start."));
    
    return _is_recording_cb(id);
}

void r_agent::_entry_point()
{
    auto default_max_sleep = milliseconds(100);

    while(_running)
    {
        if(_timer.get_num_timed_events() == 0)
        {
            _timer.add_timed_event(
                steady_clock::now(),
                seconds(60),
                std::bind(&r_agent::_process_new_or_changed_streams_configs, this),
                true
            );
        }
        auto delta_millis = _timer.update(default_max_sleep, steady_clock::now());
        this_thread::sleep_for(delta_millis);
    }
}

void r_agent::_process_new_or_changed_streams_configs()
{
    lock_guard<mutex> lock(_device_config_hashes_mutex);
    // fetch all our stream_configs from all providers. Pass _running so the
    // blocking discovery sweep aborts promptly when stop() is called — otherwise
    // stop()'s _th.join() can stall for the full multi-interface listen window.
    auto devices = _onvif_provider->poll([this]{ return _running.load(); });

    if(!devices.empty())
    {
        // populate new_or_changed with all devices that are new or changed
        vector<r_stream_config> new_or_changed;
        copy_if(
            begin(devices), end(devices),
            back_inserter(new_or_changed),
            [this](const r_stream_config& sc){
                auto new_hash = hash_stream_config(sc);
                auto found = this->_device_config_hashes.find(sc.id);
                if(found == this->_device_config_hashes.end() || new_hash != found->second)
                {
                    this->_device_config_hashes[sc.id] = new_hash;
                    return true;
                }
                return false;
            }
        );

        vector<pair<r_stream_config, string>> output;
        transform(
            begin(new_or_changed), end(new_or_changed),
            back_inserter(output),
            [](const r_stream_config& sc){
                return make_pair(sc, hash_stream_config(sc));
            }
        );

        if(_changed_streams_cb)
            _changed_streams_cb(output);

        // For every changed device, see if it's an already-assigned camera whose
        // network details just shifted (e.g. router gave it a new DHCP lease).
        // The ipv4/port get updated by _changed_streams_cb above, but the stored
        // rtsp_url still has the OLD host baked in (rtsp_url is set only by
        // interrogate_camera, never by the discovery poll). Schedule a background
        // re-interrogation so the URL gets refreshed without user intervention.
        for(const auto& sc : new_or_changed)
            _maybe_schedule_reinterrogation(sc);
    }
}

// Helper: which fields, if changed, mean we can't silently swap the camera's
// config without the user re-deciding (codec/profile/audio presence). Pure
// host/port/URL changes are considered "safe" to auto-apply.
//
// `out_reason` (if non-null) is filled with a human-readable description of
// the first field found to differ.
static bool _stream_config_compat(const r_disco::r_camera& stored, const r_disco::r_stream_config& fresh, std::string* out_reason = nullptr)
{
    auto null_or_eq = [](const auto& a, const auto& b){
        if(a.is_null() && b.is_null()) return true;
        if(a.is_null() || b.is_null()) return false;
        return a.value() == b.value();
    };
    auto to_str = [](const auto& n) -> std::string {
        if(n.is_null()) return "(null)";
        std::ostringstream oss; oss << n.value(); return oss.str();
    };
    #define _CHECK(field) \
        if(!null_or_eq(stored.field, fresh.field)) { \
            if(out_reason) *out_reason = std::string(#field) + " differs (stored=" + to_str(stored.field) + " fresh=" + to_str(fresh.field) + ")"; \
            return false; \
        }
    _CHECK(video_codec);
    _CHECK(video_timebase);
    _CHECK(audio_codec);
    _CHECK(audio_timebase);
    #undef _CHECK
    // video_codec_parameters often includes sprop-parameter-sets which change
    // each time the camera generates a new SPS/PPS (every IDR). Don't gate on
    // it. Same for audio_codec_parameters.
    return true;
}

void r_agent::_maybe_schedule_reinterrogation(const r_stream_config& new_sc)
{
    if(!_camera_lookup_cb || !_save_camera_cb || !_camera_alert_cb)
        return;  // host app didn't wire up the new callbacks; nothing to do

    auto stored_n = _camera_lookup_cb(new_sc.id);
    if(stored_n.is_null())
        return;  // not yet known (still in discovered state) — nothing to refresh
    auto stored = stored_n.value();

    // Only act on assigned (recording-eligible) cameras. Unassigned cameras
    // will pick up the new IP through the normal discovery write path.
    if(stored.state != "assigned")
        return;

    // Detect "rtsp_url has gone stale". Note that by the time we get here, the
    // _changed_streams_cb has already written the freshly-discovered ipv4 into
    // the stored record — but it does NOT touch rtsp_url (set only by the
    // interrogate_camera path). So we check whether the stored rtsp_url string
    // still references the current ipv4. If it does, URL is fresh. If it
    // doesn't, the IP changed (router DHCP swap, etc.) and the URL is now
    // pointing at the old host.
    if(stored.rtsp_url.is_null() || stored.ipv4.is_null())
        return;
    if(stored.rtsp_url.value().find(stored.ipv4.value()) != std::string::npos)
        return;

    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> g(_reinterrogation_mutex);
        auto& st = _reinterrogation_state_by_id[new_sc.id];
        if(st.in_flight)
            return;
        if(st.next_attempt > now)
            return;  // backoff window
        st.in_flight = true;
    }

    R_LOG_INFO("Network change detected for assigned camera %s — scheduling background re-interrogation",
        new_sc.id.c_str());

    // Detach the worker. It captures stored + discovered config by value so it
    // doesn't depend on the agent's polling thread, and it accesses the agent
    // only through stable references (callbacks + the onvif provider, both
    // owned by an r_agent that outlives the worker in normal app lifecycle).
    std::thread([this, new_sc, stored]() mutable {
        _do_reinterrogation(new_sc, std::move(stored));
    }).detach();
}

void r_agent::_do_reinterrogation(const r_stream_config& discovered_sc, r_camera stored)
{
    bool succeeded = false;
    try
    {
        // Drop any cached interrogation result for this id so we don't get back
        // the stale rtsp_url that pointed at the old host.
        _onvif_provider->invalidate_cache(discovered_sc.id);

        // Build a fresh stream_config: take the just-discovered network fields
        // (ipv4/port/protocol/xaddrs/address/camera_name) and let the ONVIF
        // round-trip fill in the rtsp_url + codec details using the stored
        // credentials and the user's originally-chosen profile token.
        r_stream_config sc = discovered_sc;
        std::string preferred_profile;
        if(!stored.onvif_profile_token.is_null())
            preferred_profile = stored.onvif_profile_token.value();

        _onvif_provider->interrogate_camera(
            sc,
            stored.rtsp_username,
            stored.rtsp_password,
            preferred_profile,
            /*force=*/true
        );

        // Diff vs stored. Pure URL/host changes are auto-applied; anything else
        // surfaces as an alert and the stored record stays untouched.
        std::string diff_reason;
        if(!_stream_config_compat(stored, sc, &diff_reason))
        {
            std::string msg = "Camera reconfigured (codec or profile changed). Remove + re-add to reconfigure.";
            R_LOG_WARNING("Re-interrogation of %s succeeded but config changed beyond URL — %s [diff: %s]",
                discovered_sc.id.c_str(), msg.c_str(), diff_reason.c_str());
            _camera_alert_cb(discovered_sc.id, msg);
        }
        else
        {
            // Update only the fields the discovery + interrogation actually produced.
            // Leave the user-chosen recording settings (file path, motion detection,
            // retention, etc.) intact.
            if(!sc.camera_name.is_null())               stored.camera_name = sc.camera_name;
            if(!sc.ipv4.is_null())                      stored.ipv4 = sc.ipv4;
            if(!sc.xaddrs.is_null())                    stored.xaddrs = sc.xaddrs;
            if(!sc.address.is_null())                   stored.address = sc.address;
            if(!sc.rtsp_url.is_null())                  stored.rtsp_url = sc.rtsp_url;
            if(!sc.video_codec.is_null())               stored.video_codec = sc.video_codec;
            if(!sc.video_codec_parameters.is_null())    stored.video_codec_parameters = sc.video_codec_parameters;
            if(!sc.video_timebase.is_null())            stored.video_timebase = sc.video_timebase;
            if(!sc.audio_codec.is_null())               stored.audio_codec = sc.audio_codec;
            if(!sc.audio_codec_parameters.is_null())    stored.audio_codec_parameters = sc.audio_codec_parameters;
            if(!sc.audio_timebase.is_null())            stored.audio_timebase = sc.audio_timebase;
            stored.stream_config_hash = hash_stream_config(sc);

            _save_camera_cb(stored);
            _camera_alert_cb(discovered_sc.id, "");  // clear any prior alert
            R_LOG_INFO("Re-interrogation of %s succeeded — updated rtsp_url (host change auto-applied)",
                discovered_sc.id.c_str());
        }
        succeeded = true;
    }
    catch(const std::exception& e)
    {
        R_LOG_ERROR("Background re-interrogation failed for %s: %s",
            discovered_sc.id.c_str(), e.what());
        // Don't surface the alert on first failure — discovery may have caught
        // the camera mid-reboot. Wait until a few attempts have failed before
        // bothering the user.
        std::lock_guard<std::mutex> g(_reinterrogation_mutex);
        auto& st = _reinterrogation_state_by_id[discovered_sc.id];
        if(st.attempt_count >= 2)  // 3rd consecutive failure
        {
            _camera_alert_cb(discovered_sc.id,
                std::string("Camera unreachable for re-interrogation: ") + e.what());
        }
    }

    // Update backoff state. On success: reset. On failure: exponential, capped 5 min.
    {
        std::lock_guard<std::mutex> g(_reinterrogation_mutex);
        auto& st = _reinterrogation_state_by_id[discovered_sc.id];
        st.in_flight = false;
        if(succeeded)
        {
            st.attempt_count = 0;
            st.next_attempt = std::chrono::steady_clock::time_point{};
        }
        else
        {
            st.attempt_count++;
            // Parens around std::min to suppress the Windows.h min() macro.
            int shift = (std::min)(8, st.attempt_count - 1);
            int delay_seconds = (std::min)(300, 1 << shift);
            st.next_attempt = std::chrono::steady_clock::now() + std::chrono::seconds(delay_seconds);
        }
    }
}
