
#include "r_disco/providers/r_onvif_provider.h"
#include "r_disco/r_agent.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_stream_info.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_md5.h"
#include "r_utils/r_uuid.h"
#include <string>

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

void r_onvif_provider::interrogate_camera(
    r_stream_config& sc,
    r_utils::r_nullable<std::string> username,
    r_utils::r_nullable<std::string> password
)
{
    _cache_check_expiration(sc.id);

    auto it = _cache.find(sc.id);

    if(it == _cache.end())
    {
        if(_agent && _agent->_is_recording(sc.id))
            return;

        r_onvif::r_onvif_cam cam(sc.ipv4.value(), 80, "http", sc.xaddrs.value(), username, password);

        auto caps = cam.get_camera_capabilities();
        auto oms = cam.get_media_service(caps);
        auto profile_tokens = cam.get_profile_tokens(oms);
        auto stream_uri = cam.get_stream_uri(oms, profile_tokens[0].token);

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

        _r_onvif_provider_cache_entry cache_entry;
        cache_entry.created = steady_clock::now();
        cache_entry.config = sc;
        _cache[sc.id] = cache_entry;
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
    r_utils::r_nullable<std::string> password
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

        r_onvif::r_onvif_cam cam(config.ipv4.value(), 80, "http", xaddrs, username, password);

        auto caps = cam.get_camera_capabilities();
        auto oms = cam.get_media_service(caps);
        auto profile_tokens = cam.get_profile_tokens(oms);
        auto stream_uri = cam.get_stream_uri(oms, profile_tokens[0].token);

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

        _r_onvif_provider_cache_entry cache_entry;
        cache_entry.created = steady_clock::now();
        cache_entry.config = config;
        _cache[id] = cache_entry;
    }
    else
    {
        config = it->second.config;
    }

    config_nullable.set_value(config);

    return config_nullable;

}

vector<r_stream_config> r_onvif_provider::_fetch_configs(const string& top_dir)
{
    std::vector<r_stream_config> configs;

    auto envelopes = r_onvif::discover(r_uuid::generate());

    auto discovered_infos = r_onvif::filter_discovered(envelopes);

    for(auto& di : discovered_infos)
    {
        try
        {
            r_stream_config config;
            
            // Onvif device id's are created by hashing the devices address.
            r_md5 hash;
            hash.update((uint8_t*)di.address.c_str(), di.address.size());
            hash.finalize();
            auto id = hash.get_as_uuid();
            auto credentials = _agent->_get_credentials(id);
            
            config.id = id;
            config.camera_name.set_value(di.camera_name);
            config.ipv4.set_value(di.host);
            config.xaddrs.set_value(di.uri);
            config.address.set_value(di.address);
            configs.push_back(config);
        }
        catch(const std::exception& e)
        {
            R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
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
