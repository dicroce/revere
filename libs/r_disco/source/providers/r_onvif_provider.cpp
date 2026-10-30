
#include <thread>
#include "r_disco/providers/r_onvif_provider.h"
#include "r_disco/r_agent.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_stream_info.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_md5.h"
#include "r_utils/r_uuid.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_logger.h"
#include <string>
#include <set>
#include <memory>
#include <mutex>

using namespace r_disco;
using namespace r_utils;
using namespace r_utils::r_string_utils;
using namespace r_pipeline;
using namespace std;
using namespace std::chrono;

r_onvif_provider::r_onvif_provider(const string& top_dir, r_agent* agent) :
    _top_dir(top_dir),
    _agent(agent),
    _cache()
{
}

r_onvif_provider::~r_onvif_provider()
{
}

vector<r_stream_config> r_onvif_provider::poll(const std::function<bool()>& should_continue)
{
    return _fetch_configs(_top_dir, should_continue);
}

void r_onvif_provider::invalidate_cache(const std::string& id)
{
    _cache.erase(id);
}

vector<r_onvif::onvif_profile_info> r_onvif_provider::get_camera_profiles(
    const string& ipv4,
    const string& xaddrs,
    r_nullable<string> username,
    r_nullable<string> password,
    r_nullable<int> onvif_port,
    r_nullable<string> onvif_protocol
)
{
    auto ep = r_onvif::find_device_service(ipv4, xaddrs, onvif_port, onvif_protocol);
    if(ep.is_null())
        R_THROW(("No ONVIF device service answers at %s. Check that ONVIF is enabled on the camera.", ipv4.c_str()));
    r_onvif::r_onvif_cam cam(ep.value().host, ep.value().port, ep.value().protocol, ep.value().uri, username, password);
    auto caps = cam.get_camera_capabilities();
    auto oms = cam.get_media_service(caps);
    return cam.get_profile_tokens(oms);
}

vector<r_onvif::onvif_ptz_preset> r_onvif_provider::ptz_control(
    const string& ipv4,
    const string& xaddrs,
    r_nullable<string> username,
    r_nullable<string> password,
    const r_ptz_command& cmd,
    r_nullable<int> onvif_port,
    r_nullable<string> onvif_protocol
)
{
    shared_ptr<_ptz_session> session;
    {
        lock_guard<mutex> g(_ptz_sessions_lock);
        // Keyed by where the camera answers: two cameras can share an address on different ports.
        auto& slot = _ptz_sessions[ipv4 + "|" + (onvif_port.is_null() ? string() : to_string(onvif_port.value())) + "|" + xaddrs];
        if(!slot)
            slot = make_shared<_ptz_session>();
        session = slot;
    }

    // One command at a time per camera; other cameras and interrogation don't wait.
    lock_guard<mutex> camera_lock(session->lock);

    // New credentials mean a new session: the old one negotiated with the old ones.
    auto credentials = (username.is_null() ? string() : username.value()) + "\n" + (password.is_null() ? string() : password.value());
    if(session->credentials != credentials)
    {
        session->cam.reset();
        session->credentials = credentials;
    }

    bool reused = session->cam != nullptr;
    try
    {
        if(!session->cam)
            _open_ptz_session(*session, ipv4, xaddrs, username, password, onvif_port, onvif_protocol);
        return _run_ptz(*session, cmd);
    }
    catch(const r_ptz_error& ex)
    {
        session->cam.reset();
        // A kept session may have gone stale (the camera rebooted or moved); a new one is worth one more try.
        if(!reused)
            throw;
        R_LOG_INFO("onvif: PTZ session for %s failed (%s); opening a new one", ipv4.c_str(), ex.what());
    }

    _open_ptz_session(*session, ipv4, xaddrs, username, password, onvif_port, onvif_protocol);
    try
    {
        return _run_ptz(*session, cmd);
    }
    catch(...)
    {
        session->cam.reset();
        throw;
    }
}

void r_onvif_provider::_open_ptz_session(
    _ptz_session& session,
    const string& ipv4,
    const string& xaddrs,
    r_nullable<string> username,
    r_nullable<string> password,
    r_nullable<int> onvif_port,
    r_nullable<string> onvif_protocol
)
{
    session.cam.reset();
    try
    {
        auto ep = r_onvif::find_device_service(ipv4, xaddrs, onvif_port, onvif_protocol);
        if(ep.is_null())
            throw r_ptz_error("unreachable", "No ONVIF device service answers at " + ipv4 + ". Check that ONVIF is enabled on the camera.");
        auto cam = make_unique<r_onvif::r_onvif_cam>(ep.value().host, ep.value().port, ep.value().protocol, ep.value().uri, username, password);
        auto caps = cam->get_camera_capabilities();

        auto ptz = cam->get_ptz_service(caps);
        if(ptz.is_null())
            throw r_ptz_error("no_ptz", "Camera at " + ipv4 + " has no ONVIF PTZ service.");

        // ContinuousMove/GotoPreset take a *media* profile token (the one that
        // references the camera's PTZ configuration): the camera's first profile
        // unless a command names one (single-sensor PTZ cams have just one).
        auto profiles = cam->get_profile_tokens(cam->get_media_service(caps));
        if(profiles.empty())
            throw r_ptz_error("camera_error", "Camera at " + ipv4 + " exposes no media profiles for PTZ.");

        session.ptz_service = ptz.value();
        session.profile_token = profiles.front().token;
        session.cam = std::move(cam);
    }
    catch(const r_ptz_error&)
    {
        throw;
    }
    catch(const std::exception& ex)
    {
        throw r_ptz_error("camera_error", ex.what());
    }
}

vector<r_onvif::onvif_ptz_preset> r_onvif_provider::_run_ptz(_ptz_session& session, const r_ptz_command& cmd)
{
    const auto& profile_token = cmd.profile_token.empty() ? session.profile_token : cmd.profile_token;
    try
    {
        switch(cmd.op)
        {
            case r_ptz_command::op_t::move:
                session.cam->ptz_continuous_move(session.ptz_service, profile_token, cmd.pan, cmd.tilt, cmd.zoom, cmd.timeout);
                break;
            case r_ptz_command::op_t::stop:
                session.cam->ptz_stop(session.ptz_service, profile_token);
                break;
            case r_ptz_command::op_t::goto_preset:
                session.cam->ptz_goto_preset(session.ptz_service, profile_token, cmd.preset_token);
                break;
            case r_ptz_command::op_t::list_presets:
                return session.cam->get_ptz_presets(session.ptz_service, profile_token);
        }
    }
    catch(const std::exception& ex)
    {
        throw r_ptz_error("camera_error", ex.what());
    }
    return {};
}

void r_onvif_provider::interrogate_camera(
    r_stream_config& sc,
    r_utils::r_nullable<std::string> username,
    r_utils::r_nullable<std::string> password,
    const std::string& preferred_profile_token,
    bool force
)
{
    _cache_check_expiration(sc.id);

    auto it = _cache.find(sc.id);

    if(it == _cache.end())
    {
        // Normal polling avoids interrupting a recording stream, but background
        // re-interrogation (force=true) explicitly wants to re-query — that's
        // the whole point of the call (e.g. the stream is failing because the
        // stored rtsp_url got stale after a router IP change).
        if(!force && _agent && _agent->_is_recording(sc.id))
            return;

        const int max_attempts = 3;
        for (int attempt = 0; attempt < max_attempts; ++attempt)
        {
            try
            {
                if (attempt > 0)
                {
                    int delay_ms = 3000 * (1 << (attempt - 1)); // 3s, 6s
                    R_LOG_INFO("Camera interrogation attempt %d/%d, retrying after %dms...",
                               attempt + 1, max_attempts, delay_ms);
                    this_thread::sleep_for(chrono::milliseconds(delay_ms));
                }

                r_onvif::soap_version soap_hint = r_onvif::soap_version::unknown;
                r_onvif::auth_mode auth_hint = r_onvif::auth_mode::unknown;
                auto hint_it = _negotiated_params.find(sc.id);
                if (hint_it != _negotiated_params.end())
                {
                    soap_hint = hint_it->second.soap_ver;
                    auth_hint = hint_it->second.auth_mode;
                }

                // Discovery and re-interrogation pass a freshly discovered port;
                // find it only when it is missing.
                if(sc.port.is_null())
                {
                    auto ep = r_onvif::find_device_service(sc.ipv4.value(), sc.xaddrs.value());
                    if(ep.is_null())
                        R_THROW(("No ONVIF device service answers at %s.", sc.ipv4.value().c_str()));
                    sc.port = ep.value().port;
                    sc.protocol = ep.value().protocol;
                    sc.xaddrs = ep.value().uri;
                }
                int port = sc.port.value();
                string protocol = sc.protocol.is_null() ? "http" : sc.protocol.value();
                r_onvif::r_onvif_cam cam(sc.ipv4.value(), port, protocol, sc.xaddrs.value(), username, password, soap_hint, auth_hint);

                auto caps = cam.get_camera_capabilities();
                auto oms = cam.get_media_service(caps);
                auto profile_tokens = cam.get_profile_tokens(oms);

                if(profile_tokens.empty())
                    R_THROW(("No ONVIF profiles available for camera."));

                size_t selected_idx = 0;
                if(!preferred_profile_token.empty())
                {
                    for(size_t i = 0; i < profile_tokens.size(); ++i)
                    {
                        if(profile_tokens[i].token == preferred_profile_token)
                        {
                            selected_idx = i;
                            break;
                        }
                    }
                }
                else
                {
                    uint32_t best_resolution = 0;
                    for(size_t i = 0; i < profile_tokens.size(); ++i)
                    {
                        uint32_t resolution = (uint32_t)profile_tokens[i].width * (uint32_t)profile_tokens[i].height;
                        if(resolution > best_resolution)
                        {
                            best_resolution = resolution;
                            selected_idx = i;
                        }
                    }
                }

                R_LOG_INFO("Selected ONVIF profile %zu/%zu: %s (%dx%d)",
                           selected_idx + 1, profile_tokens.size(),
                           profile_tokens[selected_idx].encoding.c_str(),
                           profile_tokens[selected_idx].width,
                           profile_tokens[selected_idx].height);

                auto stream_uri = cam.get_stream_uri(oms, profile_tokens[selected_idx].token);
                sc.rtsp_url = stream_uri;

                auto sdp_media = fetch_sdp_media(stream_uri, username, password);

                if(sdp_media.find("video") == sdp_media.end())
                    R_THROW(("Unable to fetch video stream information for r_onvif_provider."));

                string codec_name, codec_parameters;
                int timebase;
                tie(codec_name, codec_parameters, timebase) = sdp_media_map_to_s(VIDEO_MEDIA, sdp_media);

                sc.video_codec = codec_name;
                sc.video_timebase = timebase;
                sc.video_codec_parameters.set_value(codec_parameters);

                if(sdp_media.find("audio") != sdp_media.end())
                {
                    tie(codec_name, codec_parameters, timebase) = sdp_media_map_to_s(AUDIO_MEDIA, sdp_media);
                    sc.audio_codec = codec_name;
                    sc.audio_timebase = timebase;
                    sc.audio_codec_parameters = codec_parameters;
                }

                _negotiated_params[sc.id] = {cam.get_soap_version(), cam.get_auth_mode()};

                _r_onvif_provider_cache_entry cache_entry;
                cache_entry.created = steady_clock::now();
                cache_entry.config = sc;
                _cache[sc.id] = cache_entry;

                break;
            }
            catch (const std::exception& ex)
            {
                if (attempt + 1 == max_attempts)
                {
                    R_LOG_INFO("Camera interrogation failed after %d attempts: %s", max_attempts, ex.what());
                    throw;
                }
                R_LOG_INFO("Camera interrogation attempt %d/%d failed: %s",
                           attempt + 1, max_attempts, ex.what());
            }
        }
    }
    else
    {
        sc = it->second.config;
    }
}

r_utils::r_nullable<r_stream_config> r_onvif_provider::interrogate_camera(
    const std::string& id,
    const std::string& camera_name,
    const std::string& ipv4,
    const std::string& xaddrs,
    const std::string& address,
    r_utils::r_nullable<std::string> username,
    r_utils::r_nullable<std::string> password,
    const std::string& preferred_profile_token,
    r_utils::r_nullable<int> onvif_port,
    r_utils::r_nullable<std::string> onvif_protocol
)
{
    r_nullable<r_stream_config> config_nullable;
    r_stream_config config;

    config.id = id;
    config.camera_name.set_value(camera_name);
    config.ipv4 = ipv4;
    config.xaddrs = xaddrs;
    config.address = address;
    config.port = onvif_port;
    config.protocol = onvif_protocol;

    _cache_check_expiration(id);

    auto it = _cache.find(id);

    if(it == _cache.end())
    {
        if(_agent && _agent->_is_recording(config.id))
            return r_nullable<r_stream_config>();

        // Once, before the retries: where the device service answers. The
        // stored port is checked and found again if stale, and the result is
        // saved with the camera through the returned config.
        auto ep = r_onvif::find_device_service(ipv4, xaddrs, config.port, config.protocol);
        if(ep.is_null())
            R_THROW(("No ONVIF device service answers at %s. Check that ONVIF is enabled on the camera.", ipv4.c_str()));
        config.port = ep.value().port;
        config.protocol = ep.value().protocol;
        config.xaddrs = ep.value().uri;

        const int max_attempts = 3;
        for (int attempt = 0; attempt < max_attempts; ++attempt)
        {
            try
            {
                if (attempt > 0)
                {
                    int delay_ms = 3000 * (1 << (attempt - 1)); // 3s, 6s
                    R_LOG_INFO("Camera interrogation attempt %d/%d, retrying after %dms...",
                               attempt + 1, max_attempts, delay_ms);
                    this_thread::sleep_for(chrono::milliseconds(delay_ms));
                }

                r_onvif::soap_version soap_hint = r_onvif::soap_version::unknown;
                r_onvif::auth_mode auth_hint = r_onvif::auth_mode::unknown;
                auto hint_it = _negotiated_params.find(id);
                if (hint_it != _negotiated_params.end())
                {
                    soap_hint = hint_it->second.soap_ver;
                    auth_hint = hint_it->second.auth_mode;
                }

                r_onvif::r_onvif_cam cam(config.ipv4.value(), config.port.value(), config.protocol.value(), config.xaddrs.value(), username, password, soap_hint, auth_hint);

                auto caps = cam.get_camera_capabilities();
                auto oms = cam.get_media_service(caps);
                auto profile_tokens = cam.get_profile_tokens(oms);

                if(profile_tokens.empty())
                    R_THROW(("No ONVIF profiles available for camera."));

                size_t selected_idx = 0;
                if(!preferred_profile_token.empty())
                {
                    for(size_t i = 0; i < profile_tokens.size(); ++i)
                    {
                        if(profile_tokens[i].token == preferred_profile_token)
                        {
                            selected_idx = i;
                            break;
                        }
                    }
                }
                else
                {
                    uint32_t best_resolution = 0;
                    for(size_t i = 0; i < profile_tokens.size(); ++i)
                    {
                        uint32_t resolution = (uint32_t)profile_tokens[i].width * (uint32_t)profile_tokens[i].height;
                        if(resolution > best_resolution)
                        {
                            best_resolution = resolution;
                            selected_idx = i;
                        }
                    }
                }

                R_LOG_INFO("Selected ONVIF profile %zu/%zu: %s (%dx%d)",
                           selected_idx + 1, profile_tokens.size(),
                           profile_tokens[selected_idx].encoding.c_str(),
                           profile_tokens[selected_idx].width,
                           profile_tokens[selected_idx].height);

                auto stream_uri = cam.get_stream_uri(oms, profile_tokens[selected_idx].token);
                config.rtsp_url = stream_uri;

                auto sdp_media = fetch_sdp_media(stream_uri, username, password);

                if(sdp_media.find("video") == sdp_media.end())
                    R_THROW(("Unable to fetch video stream information for r_onvif_provider."));

                string codec_name, codec_parameters;
                int timebase;
                tie(codec_name, codec_parameters, timebase) = sdp_media_map_to_s(VIDEO_MEDIA, sdp_media);

                config.video_codec = codec_name;
                config.video_timebase = timebase;
                config.video_codec_parameters.set_value(codec_parameters);

                if(sdp_media.find("audio") != sdp_media.end())
                {
                    tie(codec_name, codec_parameters, timebase) = sdp_media_map_to_s(AUDIO_MEDIA, sdp_media);
                    config.audio_codec = codec_name;
                    config.audio_timebase = timebase;
                    config.audio_codec_parameters = codec_parameters;
                }

                _negotiated_params[id] = {cam.get_soap_version(), cam.get_auth_mode()};

                _r_onvif_provider_cache_entry cache_entry;
                cache_entry.created = steady_clock::now();
                cache_entry.config = config;
                _cache[id] = cache_entry;

                break;
            }
            catch (const std::exception& ex)
            {
                if (attempt + 1 == max_attempts)
                {
                    R_LOG_INFO("Camera interrogation failed after %d attempts: %s", max_attempts, ex.what());
                    throw;
                }
                R_LOG_INFO("Camera interrogation attempt %d/%d failed: %s",
                           attempt + 1, max_attempts, ex.what());
            }
        }
    }
    else
    {
        config = it->second.config;
    }

    config_nullable.set_value(config);

    return config_nullable;

}

// Build the list of unicast sweep targets: every usable host on each local
// IPv4 subnet, minus our own adapter addresses. Subnets larger than the cap are
// skipped (and logged) so we never blast an enormous range. Host-only virtual
// adapters (WSL is a /20) are skipped, and subnets shared by two adapters (wired
// + Wi-Fi on the same LAN) are swept once.
static std::vector<std::string> _subnet_sweep_targets()
{
    static const size_t MAX_SWEEP_HOSTS = 4096;  // ~/20; covers typical /24../22 LANs
    std::vector<std::string> targets;
    std::set<std::string> own;
    std::set<std::string> queued;

    auto adapters = r_utils::r_networking::r_get_adapters();
    for(auto& a : adapters)
        own.insert(a.ipv4_addr);

    for(auto& a : adapters)
    {
        if(a.ipv4_addr.empty() || a.ipv4_netmask.empty())
            continue;
        if(a.ipv4_addr.rfind("169.254.", 0) == 0)  // skip link-local
            continue;
        if(a.is_virtual)
            continue;

        auto hosts = r_utils::r_networking::r_ipv4_subnet_hosts(a.ipv4_addr, a.ipv4_netmask, MAX_SWEEP_HOSTS);
        if(hosts.empty())
        {
            R_LOG_INFO("onvif: skipping subnet sweep on %s/%s (no usable hosts, invalid mask, or larger than %zu)",
                       a.ipv4_addr.c_str(), a.ipv4_netmask.c_str(), MAX_SWEEP_HOSTS);
            continue;
        }
        for(auto& h : hosts)
            if(own.find(h) == own.end() && queued.insert(h).second)
                targets.push_back(h);
    }
    return targets;
}

vector<r_stream_config> r_onvif_provider::_fetch_configs(const string& top_dir, const std::function<bool()>& should_continue)
{
    std::vector<r_stream_config> configs;
    std::set<std::string> seen_ids;

    // Turn raw ProbeMatch envelopes into stream_configs, de-duplicated by the
    // device's stable id (MD5 of its ONVIF EndpointReference). First responder
    // for an id wins, so multicast results take precedence over unicast ones.
    auto ingest = [&](const std::vector<std::string>& envelopes) {
        for(auto& di : r_onvif::filter_discovered(envelopes))
        {
            try
            {
                // Onvif device id's are created by hashing the devices address.
                r_md5 hash;
                hash.update((uint8_t*)di.address.c_str(), di.address.size());
                hash.finalize();
                auto id = hash.get_as_uuid();
                if(!seen_ids.insert(id).second)
                    continue;  // already have this device from an earlier probe

                r_stream_config config;
                config.id = id;
                config.camera_name.set_value(di.camera_name);
                config.ipv4.set_value(di.host);
                config.port.set_value(di.port);          // Store discovered port
                config.protocol.set_value(di.protocol);  // Store discovered protocol
                config.xaddrs.set_value(di.uri);
                config.address.set_value(di.address);
                configs.push_back(config);
            }
            catch(const std::exception& e)
            {
                R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
            }
        }
    };

    // 1. Multicast discovery (unchanged default behavior).
    ingest(r_onvif::discover(r_uuid::generate(), should_continue));

    // 2. Unicast self-heal: relocate assigned cameras that multicast didn't
    //    surface (cheap cams routinely ignore multicast Probe). No-op when the
    //    host didn't wire up the assigned-cameras callback.
    auto assigned = _agent ? _agent->_get_assigned_cameras() : std::vector<r_camera>();

    auto assigned_still_missing = [&]() {
        std::vector<r_camera> miss;
        for(auto& c : assigned)
            if(!c.id.empty() && seen_ids.find(c.id) == seen_ids.end())
                miss.push_back(c);
        return miss;
    };

    auto missing = assigned_still_missing();
    if(!missing.empty())
    {
        // 2a. Always probe each missing camera's last-known IP directly — cheap,
        //     and catches a camera that stayed put but doesn't answer multicast.
        std::vector<std::string> known_targets;
        for(auto& c : missing)
            if(!c.ipv4.is_null() && !c.ipv4.value().empty())
                known_targets.push_back(c.ipv4.value());

        if(!known_targets.empty())
        {
            R_LOG_INFO("onvif: unicast-probing %zu assigned camera(s) missing from multicast discovery",
                       known_targets.size());
            ingest(r_onvif::discover_unicast(r_uuid::generate(), known_targets, should_continue));
        }

        // 2b. If still missing, the camera likely changed IP (new DHCP lease).
        //     Sweep the local subnet(s) so it's re-discovered by stable id at
        //     its new address; the agent's background re-interrogation then
        //     rewrites the rtsp_url and the stream rebuilds automatically.
        //     This re-runs each poll (~60s) while a camera stays missing — e.g.
        //     one that's powered off — so the burst is intentionally bounded:
        //     capped host count, a ~2s receive window, and aborts on shutdown.
        auto still = assigned_still_missing();
        if(!still.empty() && (!should_continue || should_continue()))
        {
            auto sweep_targets = _subnet_sweep_targets();
            if(!sweep_targets.empty())
            {
                R_LOG_INFO("onvif: %zu assigned camera(s) still missing; unicast-sweeping %zu subnet host(s) to relocate",
                           still.size(), sweep_targets.size());
                ingest(r_onvif::discover_unicast(r_uuid::generate(), sweep_targets, should_continue));
            }
        }
    }

    return configs;
}

void r_onvif_provider::_cache_check_expiration(const string& id)
{
    auto it = _cache.find(id);
    if(it != _cache.end())
    {
        if(duration_cast<minutes>(steady_clock::now() - it->second.created).count() > 60 + (rand() % 10))
            _cache.erase(it);
    }
}
