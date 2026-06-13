
#ifndef __r_onvif_r_onvif_session_h
#define __r_onvif_r_onvif_session_h

#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <array>
#include <vector>
#include <stdbool.h>
#include <functional>
#include <pugixml.hpp>

namespace r_onvif
{

// should_continue, if set, is polled between (and during) per-interface probes;
// returning false aborts discovery early. Used so a shutting-down caller isn't
// forced to wait out the full multi-interface sweep. Empty = run to completion.
R_API std::vector<std::string> discover(const std::string& uuid, const std::function<bool()>& should_continue = {});

// Unicast variant of discover(): sends the same WS-Discovery Probe directly to
// each target IP (UDP port 3702) instead of to the multicast group. Cheap ONVIF
// cameras commonly ignore multicast Probe requests (they only multicast a Hello
// at boot), so multicast discovery can miss a camera that is up and reachable.
// The raw responses are in the same ProbeMatch format as discover(), so the
// result feeds through filter_discovered() identically.
R_API std::vector<std::string> discover_unicast(const std::string& uuid, const std::vector<std::string>& target_ips, const std::function<bool()>& should_continue = {});

struct discovered_info
{
    std::string host;
    int port;
    std::string protocol;
    std::string uri;
    std::string address;
    std::string camera_name;
};

R_API std::vector<discovered_info> filter_discovered(const std::vector<std::string>& discovered);

typedef std::string onvif_capabilities;
typedef std::string onvif_media_service;
typedef std::string onvif_profile_token;

struct onvif_profile_info
{
    onvif_profile_token token;
    std::string encoding;
    uint16_t width;
    uint16_t height;
};

enum class soap_version
{
    soap_1_2,   // SOAP 1.2 (default, modern)
    soap_1_1,   // SOAP 1.1 (legacy fallback)
    unknown     // Not yet determined
};

enum class auth_mode
{
    digest,     // PasswordDigest (SHA-1, default)
    text,       // PasswordText (plaintext, fallback for buggy cameras)
    unknown     // Not yet determined
};

class r_onvif_cam
{
public:
    R_API r_onvif_cam(const std::string& host, int port, const std::string& protocol, const std::string& uri,
                      const r_utils::r_nullable<std::string>& username, const r_utils::r_nullable<std::string>& password,
                      soap_version soap_hint = soap_version::unknown,
                      auth_mode auth_hint = auth_mode::unknown);

    R_API time_t get_camera_system_date_and_time(
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password
    );

    R_API onvif_capabilities get_camera_capabilities();

    R_API onvif_media_service get_media_service(const onvif_capabilities& capabilities) const;

    R_API std::vector<onvif_profile_info> get_profile_tokens(onvif_media_service media_service);

    R_API std::string get_stream_uri(onvif_media_service media_service, onvif_profile_token profile_token);

    soap_version get_soap_version() const { return _soap_ver; }
    auth_mode get_auth_mode() const { return _auth_mode; }

private:
    std::pair<int, std::string> _soap_request(
        const std::string& host,
        int port,
        const std::string& uri,
        const std::string& soap_action,
        const std::function<void(pugi::xml_node&)>& build_body,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password
    );

    std::vector<std::string> _xaddrs_services;
    std::string _service_protocol;
    std::string _service_host;
    int _service_port;
    std::string _service_uri;

    r_utils::r_nullable<std::string> _username;
    r_utils::r_nullable<std::string> _password;

    int _time_offset_seconds;
    mutable soap_version _soap_ver;
    mutable auth_mode _auth_mode;
};

}

#endif
