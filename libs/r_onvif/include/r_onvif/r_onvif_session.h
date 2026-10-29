
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
    uint16_t width {0};
    uint16_t height {0};
    // The sensor the profile encodes (VideoSourceConfiguration/SourceToken). Multi-lens, fisheye and dual-sensor
    // cameras have several; a substream must share main's.
    std::string video_source_token;
    // The VideoEncoderConfiguration's settings, as the camera states them: hints only, since the stream itself (its
    // SDP and SPS) is what counts. Empty or 0 when the camera doesn't give one.
    std::string h264_profile;  // Baseline, Main, Extended or High
    int gov_length {0};        // frames between keyframes
    int fps_limit {0};
    int bitrate_limit {0};     // kbit/s
};

// A Media1 GetProfilesResponse: one onvif_profile_info per Profiles, in the camera's order. Throws
// std::runtime_error when the response isn't XML.
R_API std::vector<onvif_profile_info> parse_profiles_response(const std::string& xml);

// A camera's substream (its second, smaller stream, for live view and analysis): no narrower than this.
constexpr int SUBSTREAM_MIN_WIDTH = 320;
// The size a substream is ranked against: 640x480.
constexpr int64_t SUBSTREAM_TARGET_PIXELS = 640 * 480;

// The profiles that could be `main`'s substream, best first. Each is another profile of the same video source,
// H.264 or H.265 (MJPEG and MPEG-4 can't be), at most a quarter of main's pixels and at least SUBSTREAM_MIN_WIDTH
// wide. Aspect isn't tested: a 16:9 main's substream is often a squeezed 640x480 or 704x576 of the same view.
// Ranked by closeness to 640x480 (640 wide or more first between equals), then H.264 over H.265, then Baseline or
// Main over High, then the higher frame rate limit. ONVIF's word is a hint: the stream's own SDP and SPS decide
// whether a candidate is used.
R_API std::vector<onvif_profile_info> substream_candidates(const std::vector<onvif_profile_info>& profiles,
                                                           const onvif_profile_info& main);

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

// Cell motion regions. A tt:CellMotionDetector rule's ActiveCells is one bit
// per cell of its motion engine's CellLayout (Columns x Rows, row by row, most
// significant bit first), PackBits-compressed, base64.
R_API std::vector<uint8_t> packbits_decode(const std::vector<uint8_t>& in);
R_API std::vector<uint8_t> packbits_encode(const std::vector<uint8_t>& in);
// No cell set among the first `cells` (a mask that doesn't decode counts as empty).
R_API bool cell_mask_empty(const std::string& active_cells, int cells);
// Every cell of a columns x rows layout set.
R_API std::string full_cell_mask(int columns, int rows);

struct onvif_cell_motion_rule
{
    std::string configuration_token;
    std::string rule_name;
    int columns {0};             // its configuration's CellLayout; 0 x 0 when it has none
    int rows {0};
    std::string active_cells;
};

// Each tt:CellMotionDetector rule in a GetVideoAnalyticsConfigurations response.
R_API std::vector<onvif_cell_motion_rule> parse_cell_motion_rules(const std::string& xml);

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

    // Analytics (motion regions and rules). The analytics service's XAddr from
    // GetCapabilities, null if the camera has none. The rest return the
    // camera's raw response and throw on a SOAP fault.
    R_API r_utils::r_nullable<std::string> get_analytics_service(const onvif_capabilities& capabilities) const;
    R_API std::string get_video_analytics_configurations(const onvif_media_service& media_service);
    R_API std::string get_supported_rules(const std::string& analytics_service, const std::string& configuration_token);
    R_API std::string get_rules(const std::string& analytics_service, const std::string& configuration_token);
    R_API std::string get_analytics_modules(const std::string& analytics_service, const std::string& configuration_token);

    // Sets every tt:CellMotionDetector rule whose region is empty (no cell
    // active: the camera detects nothing) to the whole frame, then reads it
    // back. A rule with any region drawn is left alone, so a user's region is
    // never overwritten. Returns the rules it set ("configuration/rule").
    // Nothing for a camera without analytics (Reolinks have none over ONVIF).
    // Throws if the camera rejects the change or doesn't keep it.
    R_API std::vector<std::string> fill_empty_cell_motion_regions(const onvif_capabilities& capabilities);

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
