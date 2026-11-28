
#include "r_disco/r_agent.h"
#include "r_onvif/r_onvif_session.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_stream_probe.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_md5.h"
#include "r_utils/r_socket.h"
#include <set>
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
    _unconfirmed_addresses = load_unconfirmed_addresses(top_dir);
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

namespace
{
struct onvif_media
{
    unique_ptr<r_onvif::r_onvif_cam> cam;
    r_onvif::onvif_media_service oms;
    vector<r_onvif::onvif_profile_info> profiles;
};

// Asks the camera for its profiles; throws when it can't be asked.
onvif_media _ask_camera(const r_camera& camera, const r_nullable<string>& username, const r_nullable<string>& password)
{
    if(camera.ipv4.is_null() || camera.xaddrs.is_null())
        R_THROW(("The camera has no ONVIF address."));

    auto ep = r_onvif::find_device_service(camera.ipv4.value(), camera.xaddrs.value(), camera.onvif_port, camera.onvif_protocol);
    if(ep.is_null())
        R_THROW(("No ONVIF device service answers at %s.", camera.ipv4.value().c_str()));

    onvif_media m;
    m.cam = make_unique<r_onvif::r_onvif_cam>(ep.value().host, ep.value().port, ep.value().protocol, ep.value().uri, username, password);
    m.oms = m.cam->get_media_service(m.cam->get_camera_capabilities());
    m.profiles = m.cam->get_profile_tokens(m.oms);
    if(m.profiles.empty())
        R_THROW(("The camera has no ONVIF profiles."));
    return m;
}
}

r_substream_candidate_list r_agent::list_substream_candidates(
    const r_camera& camera,
    r_nullable<string> username,
    r_nullable<string> password
)
{
    auto m = _ask_camera(camera, username, password);
    r_substream_candidate_list list;
    list.main = m.profiles[main_profile_index(m.profiles, camera.onvif_profile_token)];
    list.candidates = r_onvif::substream_candidates(m.profiles, list.main);
    return list;
}

r_substream_choice r_agent::find_camera_substream(
    r_camera& camera,
    r_nullable<string> username,
    r_nullable<string> password,
    uint16_t main_width,
    uint16_t main_height,
    const string& only_profile_token
)
{
    r_substream_choice choice;
    try
    {
        auto m = _ask_camera(camera, username, password);
        const auto main = m.profiles[main_profile_index(m.profiles, camera.onvif_profile_token)];
        if(main_width == 0 || main_height == 0)
        {
            main_width = (uint16_t)main.width;
            main_height = (uint16_t)main.height;
        }

        // A profile the user chose is the only candidate (it still has to pass the same checks).
        vector<r_onvif::onvif_profile_info> profiles = m.profiles;
        if(!only_profile_token.empty())
        {
            profiles.clear();
            for(auto& p : m.profiles)
            {
                if(p.token == main.token || p.token == only_profile_token)
                    profiles.push_back(p);
            }
        }

        r_substream_camera access;
        access.stream_uri = [&](const r_onvif::onvif_profile_token& token){ return m.cam->get_stream_uri(m.oms, token); };
        access.probe = [&](const string& url){ return r_pipeline::probe_stream(url, username, password); };
        choice = find_substream(profiles, main, main_width, main_height, access);

        if(choice.found)
        {
            // The substream's codec parameters and timebase come from its SDP, as main's do.
            auto sdp_media = r_pipeline::fetch_sdp_media(choice.rtsp_url, username, password);
            if(sdp_media.find("video") == sdp_media.end())
            {
                choice = r_substream_choice();
                choice.reason = "no video in its description";
            }
            else
            {
                string codec, parameters;
                int timebase = 0;
                tie(codec, parameters, timebase) = r_pipeline::sdp_media_map_to_s(r_pipeline::VIDEO_MEDIA, sdp_media);
                substream_set(camera, choice, codec, parameters, timebase);
                return choice;
            }
        }
        substream_clear(camera, choice.reason);
    }
    catch(const exception& e)
    {
        R_LOG_INFO("substream: couldn't look for it: %s", e.what());
        choice = r_substream_choice();
        choice.reason = "couldn't ask the camera";
        substream_clear(camera, choice.reason);
    }
    return choice;
}

r_ptz_result r_agent::ptz_control(
    const string& ipv4,
    const string& xaddrs,
    r_nullable<string> username,
    r_nullable<string> password,
    const r_ptz_command& cmd,
    r_nullable<int> onvif_port,
    r_nullable<string> onvif_protocol
)
{
    // Not under _interrogation_mutex: the provider serializes each camera's PTZ
    // commands itself, so a move never waits on another camera's interrogation.
    return _onvif_provider->ptz_control(ipv4, xaddrs, username, password, cmd, onvif_port, onvif_protocol);
}

r_onvif::onvif_video_flip r_agent::imaging_control(
    const string& ipv4,
    const string& xaddrs,
    r_nullable<string> username,
    r_nullable<string> password,
    const r_imaging_command& cmd,
    r_nullable<int> onvif_port,
    r_nullable<string> onvif_protocol
)
{
    // Not under _interrogation_mutex, as PTZ isn't: a settings change shouldn't wait on another camera.
    return _onvif_provider->imaging_control(ipv4, xaddrs, username, password, cmd, onvif_port, onvif_protocol);
}

r_onvif::onvif_time_sync r_agent::sync_camera_time(
    const string& ipv4,
    const string& xaddrs,
    r_nullable<string> username,
    r_nullable<string> password,
    r_nullable<int> onvif_port,
    r_nullable<string> onvif_protocol
)
{
    // Not under _interrogation_mutex, as imaging_control isn't.
    return _onvif_provider->sync_camera_time(ipv4, xaddrs, username, password, onvif_port, onvif_protocol);
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
    {
        lock_guard<mutex> lock(_device_config_hashes_mutex);
        _device_config_hashes.erase(id);
    }
    // A forgotten camera's unconfirmed address goes with it: added again, it's a camera a person chose, and its MAC
    // is noted where it is.
    _set_unconfirmed_address(id, string());
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
    // A camera discovery never reports as new or changed (assigned after it was first seen, or added by its RTSP URL)
    // would otherwise have no MAC until the next restart, and its first move would be held for want of one. Every
    // device this pass saw, changed or not, vouches for its own address.
    std::map<std::string, std::string> seen_at;
    for(const auto& sc : devices)
        if(!sc.ipv4.is_null())
            seen_at[sc.id] = sc.ipv4.value();
    note_missing_macs(seen_at);
    check_stored_addresses();
}

namespace
{
int64_t _now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
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
            R_LOG_WARNING("Camera %s answers discovery at a new address (%s -> %s). It is followed now if the new "
                          "address has the camera's MAC address; otherwise it waits to be confirmed in the camera's "
                          "properties (or the app), and the camera stays at its current address meanwhile.",
                          sc.id.c_str(), move.old_ipv4.c_str(), move.new_ipv4.c_str());
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
        _note_mac(id);
}

bool r_agent::_note_mac(const std::string& id)
{
    if(!_camera_lookup_cb || !_save_camera_cb)
        return false;
    auto cam_n = _camera_lookup_cb(id);
    if(cam_n.is_null() || cam_n.value().ipv4.is_null() || !cam_n.value().mac.is_null())
        return false;
    auto cam = cam_n.value();
    // Followed here without pausing and with no MAC to check it by: nothing vouched that this is the camera, so
    // what answers here isn't taught as its MAC. A person's confirm does that.
    {
        lock_guard<mutex> g(_moves_mutex);
        auto u = _unconfirmed_addresses.find(id);
        if(u != _unconfirmed_addresses.end() && u->second == cam.ipv4.value())
            return false;
    }
    auto lookup = _mac_lookup ? _mac_lookup : mac_lookup(_default_mac_lookup);
    auto mac = lookup(cam.ipv4.value(), cam.onvif_port.is_null() ? 80 : cam.onvif_port.value());
    if(mac.is_null())
        return false;
    cam.mac = mac;
    _save_camera_cb(cam);
    R_LOG_INFO("Camera %s: noted its MAC address, %s, at %s", id.c_str(), mac.value().c_str(), cam.ipv4.value().c_str());
    return true;
}

namespace
{
// The host an RTSP URL names (rtsp://user:pass@host:port/path -> host); empty when it has none.
std::string _rtsp_host(const std::string& url)
{
    auto at = url.find("://");
    if(at == std::string::npos)
        return std::string();
    auto rest = url.substr(at + 3);
    rest = rest.substr(0, rest.find('/'));
    auto creds = rest.rfind('@');
    if(creds != std::string::npos)
        rest = rest.substr(creds + 1);
    if(!rest.empty() && rest[0] == '[')
        return rest.substr(1, rest.find(']') - 1);
    return rest.substr(0, rest.find(':'));
}
}

void r_agent::note_missing_macs(const std::map<std::string, std::string>& seen_at)
{
    auto now = _now();
    for(const auto& cam : _get_assigned_cameras())
    {
        if(!cam.mac.is_null() || cam.ipv4.is_null())
            continue;
        // The camera itself must be at its stored address now, not just something that answers there.
        auto seen = seen_at.find(cam.id);
        bool vouched = seen != seen_at.end() && seen->second == cam.ipv4.value();
        if(!vouched && _receiving_video_cb && !cam.rtsp_url.is_null() && _rtsp_host(cam.rtsp_url.value()) == cam.ipv4.value())
            vouched = _receiving_video_cb(cam.id);
        if(!vouched)
            continue;
        {
            lock_guard<mutex> g(_moves_mutex);
            // A held move: what answers at the stored address now may be another device.
            if(_held_moves.count(cam.id))
                continue;
            auto tried = _mac_note_tries.find(cam.id);
            if(tried != _mac_note_tries.end() && now - tried->second < MAC_NOTE_RETRY_MS)
                continue;
        }
        bool noted = _note_mac(cam.id);
        lock_guard<mutex> g(_moves_mutex);
        if(noted)
            _mac_note_tries.erase(cam.id);
        else
            _mac_note_tries[cam.id] = now;
    }
}

void r_agent::check_stored_addresses()
{
    auto now = _now();
    auto lookup = _mac_lookup ? _mac_lookup : mac_lookup(_default_mac_lookup);
    auto assigned = _get_assigned_cameras();
    set<string> assigned_ids;
    for(const auto& cam : assigned)
        assigned_ids.insert(cam.id);

    // A hold for a camera that is gone (removed, or no longer assigned) is dropped.
    vector<string> gone;
    {
        lock_guard<mutex> g(_moves_mutex);
        for(auto it = _address_holds.begin(); it != _address_holds.end();)
            if(!assigned_ids.count(it->first))
            {
                gone.push_back(it->first);
                _address_check_tries.erase(it->first);
                it = _address_holds.erase(it);
            }
            else
                ++it;
    }
    for(const auto& id : gone)
    {
        R_LOG_INFO("Camera %s is no longer assigned: its address hold is dropped", id.c_str());
        if(_address_check_cb)
            _address_check_cb(id, r_nullable<r_address_check>());
    }
    if(!gone.empty())
        _save_address_holds();

    for(const auto& cam : assigned)
    {
        // Only a camera with an address and a MAC on file to compare with.
        if(cam.ipv4.is_null() || cam.mac.is_null())
            continue;
        bool held = false;
        {
            lock_guard<mutex> g(_moves_mutex);
            // A held move: guarded already, and what answers at the stored address is in question.
            if(_held_moves.count(cam.id))
                continue;
            held = _address_holds.count(cam.id) > 0;
            auto tried = _address_check_tries.find(cam.id);
            if(tried != _address_check_tries.end() && now - tried->second < (held ? ADDRESS_CHECK_HELD_INTERVAL_MS : ADDRESS_CHECK_INTERVAL_MS))
                continue;
            _address_check_tries[cam.id] = now;
        }
        // Reads the network: not under the lock.
        auto mac = lookup(cam.ipv4.value(), cam.onvif_port.is_null() ? 80 : cam.onvif_port.value());
        if(mac.is_null())
            continue;
        const bool differs = mac_differs(cam.mac, mac);
        auto seen_norm = r_utils::r_networking::r_normalize_mac(mac.value());
        const string seen_mac = seen_norm.is_null() ? mac.value() : seen_norm.value();

        r_nullable<r_address_check> told;
        bool changed = false;
        {
            lock_guard<mutex> g(_moves_mutex);
            auto hold = _address_holds.find(cam.id);
            if(differs)
            {
                // A device is there that isn't the camera: held (again, for a different device than the last).
                if(hold != _address_holds.end() && hold->second.seen_mac == seen_mac)
                    continue;
                _address_hold h;
                h.check.ipv4 = cam.ipv4.value();
                h.check.since_ms = now;
                h.check.seen_mac = seen_mac;
                h.seen_mac = seen_mac;
                _address_holds[cam.id] = h;
                told.set_value(h.check);
                changed = true;
                R_LOG_WARNING("Camera %s: a different device answers at its address %s (MAC %s, on file %s); recording is stopped "
                              "until it is confirmed to be the camera", cam.id.c_str(), cam.ipv4.value().c_str(), seen_mac.c_str(),
                              cam.mac.value().c_str());
            }
            else if(hold != _address_holds.end())
            {
                // The camera's own MAC answers at its address again.
                _address_holds.erase(hold);
                changed = true;
                R_LOG_INFO("Camera %s: its own MAC address answers at %s again; recording carries on", cam.id.c_str(), cam.ipv4.value().c_str());
                // told stays null: the host is told it is released, below, outside the lock.
            }
            else
                continue;
        }
        if(changed)
            _save_address_holds();
        if(_address_check_cb)
            _address_check_cb(cam.id, told);
    }
}

bool r_agent::is_address_held(const std::string& camera_id) const
{
    lock_guard<mutex> g(_moves_mutex);
    return _address_holds.count(camera_id) > 0;
}

void r_agent::restore_address_holds()
{
    auto saved = load_address_holds(_top_dir);
    vector<pair<string, r_address_check>> restored;
    {
        lock_guard<mutex> g(_moves_mutex);
        for(const auto& [id, r] : saved)
        {
            _address_hold h;
            h.check.ipv4 = r.ipv4;
            h.check.since_ms = r.since_ms;
            h.check.seen_mac = r.seen_mac;
            h.seen_mac = r.seen_mac;
            _address_holds[id] = h;
            restored.push_back({id, h.check});
        }
    }
    for(const auto& [id, check] : restored)
    {
        R_LOG_WARNING("Camera %s: held since before the restart (a different device answered at %s); it stays stopped until it is "
                      "confirmed, or its own MAC answers there again", id.c_str(), check.ipv4.c_str());
        if(_address_check_cb)
            _address_check_cb(id, r_nullable<r_address_check>(check));
    }
}

void r_agent::_save_address_holds()
{
    lock_guard<mutex> saving(_address_hold_save_mutex);
    map<string, r_address_hold_record> now;
    {
        lock_guard<mutex> g(_moves_mutex);
        for(const auto& [id, h] : _address_holds)
        {
            r_address_hold_record r;
            r.ipv4 = h.check.ipv4;
            r.since_ms = h.check.since_ms;
            r.seen_mac = h.seen_mac;
            now[id] = r;
        }
    }
    save_address_holds(_top_dir, now);
}

address_answer r_agent::confirm_address_check(const std::string& camera_id, int64_t since_ms)
{
    string seen_mac;
    {
        lock_guard<mutex> g(_moves_mutex);
        auto hold = _address_holds.find(camera_id);
        if(hold == _address_holds.end())
            return address_answer::no_hold;
        if(hold->second.check.since_ms != since_ms)
            return address_answer::changed;
        seen_mac = hold->second.seen_mac;
        _address_holds.erase(hold);
        _address_check_tries.erase(camera_id);
    }
    _save_address_holds();
    // The person vouched that what answers is the camera, so its MAC is the camera's from now on.
    if(_camera_lookup_cb && _save_camera_cb)
    {
        auto cam_n = _camera_lookup_cb(camera_id);
        if(!cam_n.is_null())
        {
            auto cam = cam_n.value();
            cam.mac.set_value(seen_mac);
            _save_camera_cb(cam);
            R_LOG_INFO("Camera %s: confirmed at %s, so its MAC is %s now", camera_id.c_str(),
                       cam.ipv4.is_null() ? "its address" : cam.ipv4.value().c_str(), seen_mac.c_str());
        }
    }
    if(_address_check_cb)
        _address_check_cb(camera_id, r_nullable<r_address_check>());
    return address_answer::ok;
}

map<string, r_address_check> r_agent::address_checks() const
{
    map<string, r_address_check> out;
    lock_guard<mutex> g(_moves_mutex);
    for(const auto& h : _address_holds)
        out[h.first] = h.second.check;
    return out;
}

void r_agent::_follow(const std::string& id, _held_move held, const std::string& why)
{
    held.move.status = "followed";
    held.move.followed_at_ms = _now();
    held.move.hold_reason.clear();
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

void r_agent::_confirm(const std::string& id, _held_move held)
{
    // The person vouched that what answers at the new address is this camera, so its MAC is the camera's from now
    // on: a stored MAC from another interface (wired and Wi-Fi differ) or an earlier device would otherwise hold
    // every later move, however often they're confirmed. Saved before the move is followed, which writes the new
    // address over the same row; read outside the lock, as it talks to the camera.
    if(_camera_lookup_cb && _save_camera_cb)
    {
        auto cam_n = _camera_lookup_cb(id);
        if(!cam_n.is_null())
        {
            auto cam = cam_n.value();
            auto lookup = _mac_lookup ? _mac_lookup : mac_lookup(_default_mac_lookup);
            auto seen = lookup(held.move.new_ipv4, held.move.new_port);
            auto norm = [](const r_nullable<string>& m) {
                return m.is_null() ? r_nullable<string>() : r_utils::r_networking::r_normalize_mac(m.value());
            };
            auto was = norm(cam.mac), now = norm(seen);
            if(was.is_null() != now.is_null() || (!was.is_null() && was.value() != now.value()))
            {
                R_LOG_INFO("Camera %s: confirmed at %s, so its MAC is %s now (was %s)", id.c_str(), held.move.new_ipv4.c_str(),
                           now.is_null() ? "unknown until it's noted there" : now.value().c_str(),
                           was.is_null() ? "none" : was.value().c_str());
                cam.mac = now;
                _save_camera_cb(cam);
            }
        }
    }
    _set_unconfirmed_address(id, string());
    _follow(id, held, "confirmed by the user");
}

void r_agent::_set_unconfirmed_address(const std::string& id, const std::string& ipv4)
{
    lock_guard<mutex> saving(_unconfirmed_save_mutex);
    std::map<string, string> now;
    {
        lock_guard<mutex> g(_moves_mutex);
        auto it = _unconfirmed_addresses.find(id);
        if(ipv4.empty())
        {
            if(it == _unconfirmed_addresses.end())
                return;
            _unconfirmed_addresses.erase(it);
        }
        else
        {
            if(it != _unconfirmed_addresses.end() && it->second == ipv4)
                return;
            _unconfirmed_addresses[id] = ipv4;
        }
        now = _unconfirmed_addresses;
    }
    save_unconfirmed_addresses(_top_dir, now);
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

        // The camera's stored MAC at the new address is what vouches for it:
        // followed at once, so an address change doesn't stop recording.
        auto new_mac = lookup(h.move.new_ipv4, h.move.new_port);
        auto reason = hold_reason_for(stored.mac, new_mac);

        if(reason.empty())
        {
            h.move.mac_check = mac_check_for(reason);
            _follow(id, h, "the new address has the camera's MAC address");
            continue;
        }

        // This computer doesn't pause recording when a camera's address changes (the default, and the cloud's
        // setting): the camera keeps recording at the new address, and the move says how its MAC compared. The
        // stored MAC stays as it was: only a person's confirm teaches a new one, so whatever answered there once
        // (another device, or one posing as the camera) never becomes the MAC the next move is checked against.
        if(!_pause_on_address_change.load())
        {
            auto shown = [](const r_nullable<string>& m) { return m.is_null() ? string("none") : m.value(); };
            h.move.mac_check = mac_check_for(reason);
            // No MAC on file: the new address isn't vouched for, so its MAC isn't noted there either.
            _set_unconfirmed_address(id, reason == "no_stored_mac" ? h.move.new_ipv4 : string());
            _follow(id, h, "this computer doesn't pause recording when a camera's address changes; its MAC " +
                    (reason == "no_stored_mac" ? string("wasn't on file")
                                               : (reason == "mac_unknown" ? string("couldn't be read there")
                                                                          : "differs there (stored " + shown(stored.mac) + ", seen " + shown(new_mac) + ")")));
            continue;
        }

        // What the host shows (and sends on) changes with the reason; said
        // once in the log per reason.
        r_nullable<r_camera_move> changed;
        {
            lock_guard<mutex> g(_moves_mutex);
            auto it = _held_moves.find(id);
            if(it != _held_moves.end() && it->second.move.hold_reason != reason)
            {
                it->second.move.hold_reason = reason;
                changed.set_value(it->second.move);
            }
        }
        if(!changed.is_null())
        {
            // The MACs are named, so a held move can be read from the log alone: stored, and seen at the new address.
            auto shown = [](const r_nullable<string>& m) { return m.is_null() ? string("none") : m.value(); };
            R_LOG_WARNING("Camera %s at %s: the move stays held, the new address's MAC %s (stored %s, seen %s)", id.c_str(), h.move.new_ipv4.c_str(),
                          reason == "no_stored_mac" ? "can't be checked (no MAC stored for the camera)"
                                                    : (reason == "mac_unknown" ? "couldn't be read" : "doesn't match"),
                          shown(stored.mac).c_str(), shown(new_mac).c_str());
            _notify_move(id, changed);
        }
    }
}

move_answer r_agent::answer_camera_move(const std::string& camera_id, const std::string& new_ipv4, bool confirm)
{
    _held_move h;
    bool follow = false;
    bool was_held = false;
    {
        lock_guard<mutex> g(_moves_mutex);
        auto held = _held_moves.find(camera_id);
        if(held != _held_moves.end())
        {
            if(held->second.move.new_ipv4 != new_ipv4)
                return move_answer::moved_again;
            was_held = true;
            if(confirm)
            {
                h = held->second;
                follow = true;
            }
            else
            {
                _dismissed_moves[camera_id] = _move_key(held->second.move);
                _held_moves.erase(held);
            }
        }
        else
        {
            // A followed move has nothing to confirm; dismissing it marks it seen.
            auto followed = _followed_moves.find(camera_id);
            if(followed == _followed_moves.end() || confirm)
                return move_answer::no_move;
            if(followed->second.new_ipv4 != new_ipv4)
                return move_answer::moved_again;
            _followed_moves.erase(followed);
        }
    }
    if(follow)
        _confirm(camera_id, h);
    else
    {
        if(was_held)
            R_LOG_INFO("Camera %s: the user kept its current address", camera_id.c_str());
        _notify_move(camera_id, r_nullable<r_camera_move>());
    }
    return move_answer::ok;
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
    _confirm(camera_id, h);
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

    // A camera held for a different device at its address: its login is not sent to that device, by this either.
    if(is_address_held(new_sc.id))
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
        // Held since it was scheduled: nothing is sent to the camera's address now.
        if(is_address_held(new_sc.id))
        {
            std::lock_guard<std::mutex> g(_reinterrogation_mutex);
            _reinterrogation_state_by_id[new_sc.id].in_flight = false;
            return;
        }
        _do_reinterrogation(new_sc, std::move(stored));
    }).detach();
}

// The camera moved, so its substream's address did too: ask for the chosen profile's address again and check
// it's still the same stream. A substream that can't be followed (the camera didn't answer) is dropped, with the
// reason, rather than kept at the old address.
static void _follow_substream(r_camera& camera)
{
    if(!substream_on(camera) || camera.sub_onvif_profile_token.is_null())
        return;
    try
    {
        auto m = _ask_camera(camera, camera.rtsp_username, camera.rtsp_password);
        const auto& token = camera.sub_onvif_profile_token.value();
        bool exists = false;
        for(auto& p : m.profiles)
            exists = exists || p.token == token;
        if(!exists)
        {
            substream_clear(camera, "camera changed the stream");
            return;
        }
        auto url = m.cam->get_stream_uri(m.oms, token);
        auto sdp_media = r_pipeline::fetch_sdp_media(url, camera.rtsp_username, camera.rtsp_password);
        if(sdp_media.find("video") == sdp_media.end())
            R_THROW(("The substream has no video."));
        string codec, parameters;
        int timebase = 0;
        tie(codec, parameters, timebase) = r_pipeline::sdp_media_map_to_s(r_pipeline::VIDEO_MEDIA, sdp_media);
        substream_follow(camera, url, codec, parameters);
    }
    catch(const exception& e)
    {
        // The stored address is the old one's. It isn't kept as "ok" for something to open and fail on: the
        // camera uses its main stream, and its settings can look again.
        R_LOG_INFO("substream: couldn't follow %s to its new address: %s", camera.id.c_str(), e.what());
        substream_clear(camera, "couldn't follow the camera to its new address");
    }
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
            _follow_substream(stored);

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
