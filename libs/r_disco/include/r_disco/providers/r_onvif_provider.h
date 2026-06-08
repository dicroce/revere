
#ifndef r_disco_r_onvif_provider_h
#define r_disco_r_onvif_provider_h

#include "r_disco/r_stream_config.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include "r_onvif/r_onvif_session.h"

#include <chrono>
#include <functional>
#include <map>

namespace r_disco
{

class r_agent;

class r_onvif_provider
{
public:
    R_API r_onvif_provider(const std::string& top_dir, r_agent* agent);
    R_API ~r_onvif_provider();

    // should_continue, if set, is polled during the (blocking, multi-interface)
    // ONVIF discovery sweep; returning false aborts it early so a shutting-down
    // caller isn't forced to wait it out. Empty = run to completion.
    R_API std::vector<r_stream_config> poll(const std::function<bool()>& should_continue = {});

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
        const std::string& preferred_profile_token = ""
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
};

}

#endif
