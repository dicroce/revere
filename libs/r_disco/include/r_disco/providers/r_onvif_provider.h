
#ifndef r_disco_r_onvif_provider_h
#define r_disco_r_onvif_provider_h

#include "r_disco/r_stream_config.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include "r_onvif/r_onvif_session.h"

#include <chrono>
#include <map>

namespace r_disco
{

class r_agent;

class r_onvif_provider
{
public:
    R_API r_onvif_provider(const std::string& top_dir, r_agent* agent);
    R_API ~r_onvif_provider();

    R_API std::vector<r_stream_config> poll();

    R_API std::vector<r_onvif::onvif_profile_info> get_camera_profiles(
        const std::string& ipv4,
        const std::string& xaddrs,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password
    );

    R_API void interrogate_camera(
        r_stream_config& sc,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        const std::string& preferred_profile_token = ""
    );

    R_API r_utils::r_nullable<r_stream_config> interrogate_camera(
        const std::string& id,
        const std::string& camera_name,
        const std::string& ipv4,
        const std::string& xaddrs,
        const std::string& address,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password,
        const std::string& preferred_profile_token = ""
    );

private:
    std::vector<r_stream_config> _fetch_configs(const std::string& top_dir);
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
};

}

#endif
