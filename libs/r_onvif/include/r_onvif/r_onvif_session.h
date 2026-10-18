
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

// Where a camera's ONVIF device service answers. Revere stores only the
// service's path (xaddrs), and the port varies: 80 on many cameras, 8000 on
// Reolinks. Tries, each with an unauthenticated GetSystemDateAndTime and a
// short timeout: hint_port (when set), then unicast WS-Discovery to the host,
// then 80, 8000 and 8080 over http and 443 over https. Null if none answers.
struct onvif_endpoint
{
    std::string host;
    int port;
    std::string protocol;
    std::string uri;
};

R_API r_utils::r_nullable<onvif_endpoint> find_device_service(
    const std::string& host,
    const std::string& uri,
    r_utils::r_nullable<int> hint_port = r_utils::r_nullable<int>(),
    r_utils::r_nullable<std::string> hint_protocol = r_utils::r_nullable<std::string>(),
    const std::function<bool()>& should_continue = {}  // polled between attempts; false stops the search
);

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

// ONVIF events (Event service, WS-BaseNotification pull point).
//
// A pull point is created on the events service and then polled at its own
// address. Many cameras hand back ReferenceParameters with the subscription
// (an id the camera needs to find it) that must be echoed as SOAP headers on
// every request to that address, alongside the WS-Addressing Action and To.
struct onvif_pull_point
{
    std::string address;                            // wsa:Address of the SubscriptionReference
    std::vector<std::string> reference_parameters;  // serialized elements to echo as SOAP headers
    std::string termination_time;                   // as the camera reported it (may be empty)
};

struct onvif_event
{
    std::string topic;              // e.g. "tns1:RuleEngine/CellMotionDetector/Motion"
    std::string utc_time;           // tt:Message/@UtcTime
    std::string property_operation; // Initialized, Changed or Deleted (may be empty)
    std::vector<std::pair<std::string, std::string>> source; // tt:Source SimpleItems (Name, Value)
    std::vector<std::pair<std::string, std::string>> data;   // tt:Data SimpleItems (Name, Value)
};

// The parsing behind the events calls, separate so it can be tested without a
// camera. Each throws std::runtime_error on a response it can't use.
//
// CreatePullPointSubscriptionResponse: the subscription's address, its
// ReferenceParameters (each serialized with the xmlns declarations it uses,
// ready to echo as a SOAP header) and the termination time.
R_API onvif_pull_point parse_create_pull_point_response(const std::string& xml);

// PullMessagesResponse: one onvif_event per NotificationMessage.
R_API std::vector<onvif_event> parse_pull_messages_response(const std::string& xml);

// GetEventPropertiesResponse: every topic in its TopicSet marked
// wstop:topic="true", as a path of local names without prefixes
// ("RuleEngine/CellMotionDetector/Motion").
R_API std::vector<std::string> parse_event_topics(const std::string& xml);

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

    // The events service's XAddr from GetCapabilities. Null if the camera has none.
    R_API r_utils::r_nullable<std::string> get_events_service(const onvif_capabilities& capabilities) const;

    // GetEventProperties: the raw response, whose TopicSet lists the topics the camera can raise.
    R_API std::string get_event_properties(const std::string& events_service);

    // CreatePullPointSubscription with an InitialTerminationTime of termination_seconds.
    R_API onvif_pull_point create_pull_point_subscription(const std::string& events_service, int termination_seconds);

    // PullMessages: waits up to timeout_seconds on the camera for up to message_limit events.
    R_API std::vector<onvif_event> pull_messages(const onvif_pull_point& pull_point, int timeout_seconds, int message_limit);

    // Renew the subscription for another termination_seconds.
    R_API void renew(const onvif_pull_point& pull_point, int termination_seconds);

    R_API void unsubscribe(const onvif_pull_point& pull_point);

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
        r_utils::r_nullable<std::string> password,
        const std::function<void(pugi::xml_node&)>& build_header = {},
        bool log_request = true,
        int timeout_ms = 30000,
        int net_attempts = 2
    );

    // A request to a pull point's own address: WS-Addressing Action and To, and
    // its ReferenceParameters, as headers.
    std::pair<int, std::string> _pull_point_request(
        const onvif_pull_point& pull_point,
        const std::string& soap_action,
        const std::function<void(pugi::xml_node&)>& build_body,
        bool log_request = true,
        int timeout_ms = 30000,
        int net_attempts = 2
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
