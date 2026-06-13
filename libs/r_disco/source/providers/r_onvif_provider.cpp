
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

vector<r_stream_config> r_onvif_provider::poll()
{
    return _fetch_configs(_top_dir);
}

void r_onvif_provider::invalidate_cache(const std::string& id)
{
    _cache.erase(id);
}

vector<r_onvif::onvif_profile_info> r_onvif_provider::get_camera_profiles(
    const string& ipv4,
    const string& xaddrs,
    r_nullable<string> username,
    r_nullable<string> password
)
{
    r_onvif::r_onvif_cam cam(ipv4, 80, "http", xaddrs, username, password);
    auto caps = cam.get_camera_capabilities();
    auto oms = cam.get_media_service(caps);
    return cam.get_profile_tokens(oms);
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

                int port = sc.port.is_null() ? 80 : sc.port.value();
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
    const std::string& preferred_profile_token
)
{
    r_nullable<r_stream_config> config_nullable;
    r_stream_config config;

    config.id = id;
    config.camera_name.set_value(camera_name);
    config.ipv4 = ipv4;
    config.xaddrs = xaddrs;
    config.address = address;

    _cache_check_expiration(id);

    auto it = _cache.find(id);

    if(it == _cache.end())
    {
        if(_agent && _agent->_is_recording(config.id))
            return r_nullable<r_stream_config>();

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

                int port = config.port.is_null() ? 80 : config.port.value();
                string protocol = config.protocol.is_null() ? "http" : config.protocol.value();
                r_onvif::r_onvif_cam cam(config.ipv4.value(), port, protocol, xaddrs, username, password, soap_hint, auth_hint);

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
// skipped (and logged) so we never blast an enormous range.
static std::vector<std::string> _subnet_sweep_targets()
{
    static const size_t MAX_SWEEP_HOSTS = 4096;  // ~/20; covers typical /24../22 LANs
    std::vector<std::string> targets;
    std::set<std::string> own;

    auto adapters = r_utils::r_networking::r_get_adapters();
    for(auto& a : adapters)
        own.insert(a.ipv4_addr);

    for(auto& a : adapters)
    {
        if(a.ipv4_addr.empty() || a.ipv4_netmask.empty())
            continue;
        if(a.ipv4_addr.rfind("169.254.", 0) == 0)  // skip link-local
            continue;

        auto hosts = r_utils::r_networking::r_ipv4_subnet_hosts(a.ipv4_addr, a.ipv4_netmask, MAX_SWEEP_HOSTS);
        if(hosts.empty())
        {
            R_LOG_INFO("onvif: skipping subnet sweep on %s/%s (no usable hosts, invalid mask, or larger than %zu)",
                       a.ipv4_addr.c_str(), a.ipv4_netmask.c_str(), MAX_SWEEP_HOSTS);
            continue;
        }
        for(auto& h : hosts)
            if(own.find(h) == own.end())
                targets.push_back(h);
    }
    return targets;
}

vector<r_stream_config> r_onvif_provider::_fetch_configs(const string& top_dir)
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
    ingest(r_onvif::discover(r_uuid::generate()));

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
            ingest(r_onvif::discover_unicast(r_uuid::generate(), known_targets));
        }

        // 2b. If still missing, the camera likely changed IP (new DHCP lease).
        //     Sweep the local subnet(s) so it's re-discovered by stable id at
        //     its new address; the agent's background re-interrogation then
        //     rewrites the rtsp_url and the stream rebuilds automatically.
        auto still = assigned_still_missing();
        if(!still.empty())
        {
            auto sweep_targets = _subnet_sweep_targets();
            if(!sweep_targets.empty())
            {
                R_LOG_INFO("onvif: %zu assigned camera(s) still missing; unicast-sweeping %zu subnet host(s) to relocate",
                           still.size(), sweep_targets.size());
                ingest(r_onvif::discover_unicast(r_uuid::generate(), sweep_targets));
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
