
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

R_API std::vector<std::string> discover(const std::string& uuid);

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
    R_API r_onvif_cam(const std::string& host, int port, const std::string& protocol, const std::string& uri, const r_utils::r_nullable<std::string>& username, const r_utils::r_nullable<std::string>& password);

    R_API time_t get_camera_system_date_and_time();

    R_API onvif_capabilities get_camera_capabilities();

    R_API onvif_media_service get_media_service(const onvif_capabilities& capabilities) const;

    R_API std::vector<onvif_profile_info> get_profile_tokens(onvif_media_service media_service);

    R_API std::string get_stream_uri(onvif_media_service media_service, onvif_profile_token profile_token);

private:
    std::pair<int, std::string> _soap_request(
        const std::string& host,
        int port,
        const std::string& uri,
        const std::string& soap_action,
        const std::function<void(pugi::xml_node&)>& build_body
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
