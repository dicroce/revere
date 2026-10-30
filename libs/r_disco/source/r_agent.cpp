
#include "r_disco/r_agent.h"
#include "r_onvif/r_onvif_session.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_md5.h"
#include "r_utils/r_socket.h"
#include <chrono>
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
        // Discovery checks _running between sends and receives, so this
        // returns within a few seconds. Log a slow one, so a stop that drags
        // shows up in the log.
        auto started = std::chrono::steady_clock::now();
        _th.join();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        if(ms > 5000)
            R_LOG_WARNING("r_agent::stop() took %lld ms waiting for the discovery thread", (long long)ms);
    }
}

vector<r_onvif::onvif_profile_info> r_agent::get_camera_profiles(
    const string& ipv4,
    const string& xaddrs,
    r_nullable<string> username,
    r_nullable<string> password,
    r_nullable<int> onvif_port,
    r_nullable<string> onvif_protocol
)
{
    lock_guard<mutex> lk(_interrogation_mutex);
    return _onvif_provider->get_camera_profiles(ipv4, xaddrs, username, password, onvif_port, onvif_protocol);
}

void r_agent::interrogate_camera(
    const std::string& camera_name,
    const std::string& ipv4,
    const std::string& xaddrs,
    const std::string& address,
    r_nullable<string> username,
    r_nullable<string> password,
    const std::string& preferred_profile_token,
    r_nullable<int> onvif_port,
    r_nullable<string> onvif_protocol
)
{
    lock_guard<mutex> lk(_interrogation_mutex);

    r_md5 hash;
    hash.update((uint8_t*)address.c_str(), address.size());
    hash.finalize();
    auto id = hash.get_as_uuid();

    auto sc = _onvif_provider->interrogate_camera(id, camera_name, ipv4, xaddrs, address, username, password, preferred_profile_token, onvif_port, onvif_protocol);

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

std::vector<r_camera> r_agent::_get_assigned_cameras()
{
    // Optional: if the host didn't wire this up, unicast self-heal simply
    // doesn't run (multicast discovery still works exactly as before).
    if(!_assigned_cameras_cb)
        return {};
    return _assigned_cameras_cb();
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

        // Written, except an assigned camera's address change, which is held
        // until it can be followed (see apply_discovered_changes).
        apply_discovered_changes(output);
    }

    recheck_held_moves();
}

namespace
{
int64_t _now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// The host and port of an rtsp:// URL (554 when it has none); false if it isn't one.
bool _rtsp_host_port(const std::string& url, std::string& host, int& port)
{
    auto scheme = url.find("://");
    if(scheme == std::string::npos)
        return false;
    auto rest = url.substr(scheme + 3);
    rest = rest.substr(0, rest.find_first_of("/?#"));
    auto at = rest.rfind('@');
    if(at != std::string::npos)
        rest = rest.substr(at + 1);
    auto colon = rest.rfind(':');
    host = colon == std::string::npos ? rest : rest.substr(0, colon);
    port = 554;
    if(colon != std::string::npos)
    {
        try { port = std::stoi(rest.substr(colon + 1)); } catch(...) { return false; }
    }
    return !host.empty();
}

// Whether a TCP connection to host:port opens within a couple of seconds.
bool _tcp_answers(const std::string& host, int port)
{
    if(host.empty() || port <= 0)
        return false;
    try
    {
        r_utils::r_socket sok;
        sok.set_io_timeout(2000);
        sok.connect(host, port);
        sok.close();
        return true;
    }
    catch(...)
    {
        return false;
    }
}

bool _default_address_alive(const r_disco::r_camera& stored)
{
    if(!stored.ipv4.is_null() && _tcp_answers(stored.ipv4.value(), stored.onvif_port.is_null() ? 80 : stored.onvif_port.value()))
        return true;
    std::string host;
    int port = 0;
    if(!stored.rtsp_url.is_null() && _rtsp_host_port(stored.rtsp_url.value(), host, port) && _tcp_answers(host, port))
        return true;
    return false;
}

r_utils::r_nullable<std::string> _default_mac_lookup(const std::string& ipv4, int port)
{
    // Talk to the host first, so the neighbour table has an entry for it.
    _tcp_answers(ipv4, port > 0 ? port : 80);
    return r_utils::r_networking::r_arp_lookup(ipv4);
}

std::string _move_key(const r_disco::r_camera_move& m)
{
    return m.new_ipv4 + "|" + std::to_string(m.new_port) + "|" + m.new_xaddrs;
}
}

int64_t r_agent::_now() const
{
    return _clock_ms ? _clock_ms() : _now_ms();
}

void r_agent::_notify_move(const std::string& id, const r_nullable<r_camera_move>& move)
{
    if(_camera_move_cb)
        _camera_move_cb(id, move);
}

void r_agent::apply_discovered_changes(const vector<pair<r_stream_config, string>>& changed)
{
    vector<pair<r_stream_config, string>> pass;
    vector<string> backfill_mac;
    for(const auto& [sc, hash] : changed)
    {
        auto stored_n = _camera_lookup_cb ? _camera_lookup_cb(sc.id) : r_nullable<r_camera>();
        auto move_n = stored_n.is_null() ? r_nullable<r_camera_move>() : camera_move_from(stored_n.value(), sc);
        if(move_n.is_null())
        {
            pass.push_back({sc, hash});
            if(!stored_n.is_null() && stored_n.value().state == "assigned")
            {
                // Back at (or still at) its stored address: any held move is moot.
                bool had_move = false;
                {
                    lock_guard<mutex> g(_moves_mutex);
                    had_move = _held_moves.erase(sc.id) > 0;
                }
                if(had_move)
                {
                    R_LOG_INFO("Camera %s answers at its stored address again; its held move is dropped", sc.id.c_str());
                    _notify_move(sc.id, r_nullable<r_camera_move>());
                }
                if(stored_n.value().mac.is_null())
                    backfill_mac.push_back(sc.id);
            }
            continue;
        }

        auto move = move_n.value();
        auto key = _move_key(move);
        bool raise = false;
        {
            lock_guard<mutex> g(_moves_mutex);
            auto dismissed = _dismissed_moves.find(sc.id);
            if(dismissed != _dismissed_moves.end() && dismissed->second == key)
                continue;  // the user kept the current address for this one
            auto held = _held_moves.find(sc.id);
            if(held != _held_moves.end() && _move_key(held->second.move) == key)
            {
                held->second.discovered = sc;  // same move, fresher details
                held->second.hash = hash;
                continue;
            }
            _held_moves[sc.id] = {sc, hash, move};
            raise = true;
        }
        if(raise)
        {
            R_LOG_WARNING("Camera %s answers discovery at a new address (%s -> %s). Not followed yet: it is followed once the "
                          "old address has been silent for %d minutes and the new one has the camera's MAC address, or "
                          "when confirmed in the camera's properties. The camera stays at its current address meanwhile.",
                          sc.id.c_str(), move.old_ipv4.c_str(), move.new_ipv4.c_str(), (int)MOVE_FOLLOW_AFTER.count());
            _notify_move(sc.id, move_n);
        }
    }

    if(!pass.empty() && _changed_streams_cb)
        _changed_streams_cb(pass);

    // For every changed device, see if it's an already-assigned camera whose
    // stored rtsp_url no longer names its address (followed or confirmed moves
    // write the new address; the rtsp_url still has the old host). Schedule a
    // background re-interrogation so the URL gets refreshed.
    for(const auto& p : pass)
        _maybe_schedule_reinterrogation(p.first);

    // A camera seen at its stored address without a MAC: note it now, so a
    // later move can be checked against it.
    for(const auto& id : backfill_mac)
    {
        if(!_camera_lookup_cb || !_save_camera_cb)
            break;
        auto cam_n = _camera_lookup_cb(id);
        if(cam_n.is_null() || cam_n.value().ipv4.is_null() || !cam_n.value().mac.is_null())
            continue;
        auto cam = cam_n.value();
        auto lookup = _mac_lookup ? _mac_lookup : mac_lookup(_default_mac_lookup);
        auto mac = lookup(cam.ipv4.value(), cam.onvif_port.is_null() ? 80 : cam.onvif_port.value());
        if(mac.is_null())
            continue;
        cam.mac = mac;
        _save_camera_cb(cam);
        R_LOG_INFO("Camera %s: noted its MAC address", id.c_str());
    }
}

void r_agent::_follow(const std::string& id, _held_move held, const std::string& why)
{
    held.move.status = "followed";
    held.move.followed_at_ms = _now();
    {
        lock_guard<mutex> g(_moves_mutex);
        _held_moves.erase(id);
        _dismissed_moves.erase(id);
        _followed_moves[id] = held.move;
    }
    R_LOG_WARNING("Camera %s moved %s -> %s: followed (%s)", id.c_str(), held.move.old_ipv4.c_str(), held.move.new_ipv4.c_str(), why.c_str());
    if(_changed_streams_cb)
        _changed_streams_cb({{held.discovered, held.hash}});
    r_nullable<r_camera_move> m;
    m.set_value(held.move);
    _notify_move(id, m);
    _maybe_schedule_reinterrogation(held.discovered);
}

void r_agent::recheck_held_moves()
{
    auto now_ms = _now();
    vector<pair<string, _held_move>> held;
    vector<string> expired;
    {
        lock_guard<mutex> g(_moves_mutex);
        for(auto& h : _held_moves)
            held.push_back(h);
        for(auto it = _followed_moves.begin(); it != _followed_moves.end();)
        {
            if(now_ms - it->second.followed_at_ms > 24LL * 3600 * 1000)
            {
                expired.push_back(it->first);
                it = _followed_moves.erase(it);
            }
            else
                ++it;
        }
    }
    for(auto& id : expired)
        _notify_move(id, r_nullable<r_camera_move>());

    auto alive_probe = _address_alive ? _address_alive : address_alive_probe(_default_address_alive);
    auto lookup = _mac_lookup ? _mac_lookup : mac_lookup(_default_mac_lookup);

    for(auto& [id, h] : held)
    {
        auto stored_n = _camera_lookup_cb ? _camera_lookup_cb(id) : r_nullable<r_camera>();
        if(stored_n.is_null() || stored_n.value().state != "assigned")
        {
            {
                lock_guard<mutex> g(_moves_mutex);
                _held_moves.erase(id);
            }
            _notify_move(id, r_nullable<r_camera_move>());
            continue;
        }
        auto stored = stored_n.value();

        bool alive = alive_probe(stored);
        if(alive)
            h.move.old_dead_since_ms = 0;
        else if(h.move.old_dead_since_ms == 0)
            h.move.old_dead_since_ms = now_ms;
        auto dead_for = std::chrono::milliseconds(alive ? 0 : now_ms - h.move.old_dead_since_ms);

        r_nullable<string> new_mac;
        if(!alive && dead_for >= MOVE_FOLLOW_AFTER)
            new_mac = lookup(h.move.new_ipv4, h.move.new_port);

        if(decide_camera_move(alive, dead_for, stored.mac, new_mac) == move_outcome::follow)
        {
            _follow(id, h, "the old address stopped answering and the new one has the camera's MAC address");
            continue;
        }

        if(!alive && dead_for >= MOVE_FOLLOW_AFTER)
            R_LOG_WARNING("Camera %s: old address silent for %lld min, but the new address's MAC %s; the move stays held",
                          id.c_str(), (long long)(dead_for.count() / 60000),
                          stored.mac.is_null() ? "can't be checked (no MAC stored for the camera)"
                                               : (new_mac.is_null() ? "couldn't be read" : "doesn't match"));

        lock_guard<mutex> g(_moves_mutex);
        auto it = _held_moves.find(id);
        if(it != _held_moves.end())
            it->second.move.old_dead_since_ms = h.move.old_dead_since_ms;
    }
}

bool r_agent::confirm_camera_move(const std::string& camera_id)
{
    _held_move h;
    {
        lock_guard<mutex> g(_moves_mutex);
        auto it = _held_moves.find(camera_id);
        if(it == _held_moves.end())
            return false;
        h = it->second;
    }
    _follow(camera_id, h, "confirmed by the user");
    return true;
}

bool r_agent::dismiss_camera_move(const std::string& camera_id)
{
    {
        lock_guard<mutex> g(_moves_mutex);
        auto it = _held_moves.find(camera_id);
        if(it == _held_moves.end())
            return false;
        _dismissed_moves[camera_id] = _move_key(it->second.move);
        _held_moves.erase(it);
    }
    R_LOG_INFO("Camera %s: the user kept its current address", camera_id.c_str());
    _notify_move(camera_id, r_nullable<r_camera_move>());
    return true;
}

void r_agent::clear_followed_move(const std::string& camera_id)
{
    bool had = false;
    {
        lock_guard<mutex> g(_moves_mutex);
        had = _followed_moves.erase(camera_id) > 0;
    }
    if(had)
        _notify_move(camera_id, r_nullable<r_camera_move>());
}

std::map<std::string, r_camera_move> r_agent::camera_moves() const
{
    lock_guard<mutex> g(_moves_mutex);
    std::map<std::string, r_camera_move> out;
    for(auto& f : _followed_moves)
        out[f.first] = f.second;
    for(auto& h : _held_moves)
        out[h.first] = h.second.move;
    return out;
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
            if(!sc.port.is_null())                      stored.onvif_port = sc.port;
            if(!sc.protocol.is_null())                  stored.onvif_protocol = sc.protocol;
            if(!sc.address.is_null())                   stored.address = sc.address;
            if(!sc.manufacturer.is_null())              stored.manufacturer = sc.manufacturer;
            if(!sc.model.is_null())                     stored.model = sc.model;
            if(!sc.firmware_version.is_null())          stored.firmware_version = sc.firmware_version;
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
