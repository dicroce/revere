
#ifndef __r_onvif_r_onvif_session_h
#define __r_onvif_r_onvif_session_h

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <array>
#include <vector>
#include <stdbool.h>

namespace r_onvif
{

struct r_onvif_discovery_info
{
    int index;
    std::string camera_name;
    std::string ipv4;
    std::string xaddrs;
    std::string address;
};

struct r_onvif_device_info
{
    std::string camera_name;
    std::string ipv4;
    std::string xaddrs;
    std::string address;
    std::string serial_number;
    std::string model_number;
    std::string firmware_version;
    std::string manufacturer;
    std::string hardware_id;
    std::string rtsp_url;
};

class r_onvif_session
{
public:
    R_API r_onvif_session();
    R_API ~r_onvif_session();

    R_API std::vector<r_onvif_discovery_info> discover();

    R_API r_utils::r_nullable<r_onvif_device_info> get_rtsp_url(
        const std::string& camera_name,
        const std::string& ipv4,
        const std::string& xaddrs,
        const std::string& address,
        r_utils::r_nullable<std::string> username,
        r_utils::r_nullable<std::string> password
    ) const;

private:
    std::string _get_discovery_xml() const;
    std::string _get_discovery_xml2() const;
    void _set_socket_options(int fd) const;
    r_utils::r_nullable<std::string> _get_scope_field(const std::string& scope, const std::string& field_name) const;
    std::string _get_camera_name(int index, const std::string& default_name) const;
    std::string _extract_xaddrs(int index) const;
    std::string _extract_address(int index) const;
    std::string _get_xml_value(xmlDocPtr doc, const std::string& xpath, const std::string& id) const;
    bool _has_xml_node(xmlDocPtr doc, const std::string& xpath) const;
    r_utils::r_nullable<std::string> _maybe_get_xml_value(xmlDocPtr doc, const std::string& xpath) const;
    std::string _get_node_attribute(xmlDocPtr doc, const std::string& xpath, const std::string& attribute) const;
    std::string _extract_onvif_service(const std::string& service, bool post) const;
    int _get_time_offset(const std::string& deviceService, const std::string& xaddrs) const;
    xmlDocPtr _send_command_to_camera(const std::string& cmd, const std::string& xaddrs, bool throw_on_unauthorized = false) const;
    std::string _add_http_header(xmlDocPtr doc, xmlNodePtr root, const std::string& xaddrs, const std::string& post_type) const;
    xmlDocPtr _send_get_capabilities(const r_utils::r_nullable<std::string> username, const r_utils::r_nullable<std::string> password, const std::string& xaddrs, int time_offset) const;
    void _check_for_xml_error_msg(xmlDocPtr doc, const std::string& xaddr) const;
    std::string _get_first_profile_token(const r_utils::r_nullable<std::string>& username, const r_utils::r_nullable<std::string>& password, const std::string& xaddrs, const std::string& media_service, int time_offset) const;
    std::string _get_stream_uri(const r_utils::r_nullable<std::string>& username, const r_utils::r_nullable<std::string>& password, int timeOffset, std::string& profileToken, const std::string& xaddrs, const std::string& mediaService) const;
    xmlDocPtr _send_get_device_information(const r_utils::r_nullable<std::string>& username, const r_utils::r_nullable<std::string>& password, int timeOffset, const std::string& xaddrs, const std::string& device_service) const;
    void add_username_digest_header(xmlNodePtr root, xmlNsPtr ns_env, const char *user, const char *password, time_t offset) const;

    char _buf[128][8192];
    int _len[128];
    std::string _uuid;
    int _discovery_msg_id;
};

}

#endif
