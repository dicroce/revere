
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
// One ProbeMatch reply and the IPv4 address it came from.
struct discovery_reply
{
    std::string sender;
    std::string envelope;
};

R_API std::vector<discovery_reply> discover(const std::string& uuid, const std::function<bool()>& should_continue = {});

// Unicast variant of discover(): sends the same WS-Discovery Probe directly to
// each target IP (UDP port 3702) instead of to the multicast group. Cheap ONVIF
// cameras commonly ignore multicast Probe requests (they only multicast a Hello
// at boot), so multicast discovery can miss a camera that is up and reachable.
// The raw responses are in the same ProbeMatch format as discover(), so the
// result feeds through filter_discovered() identically.
R_API std::vector<discovery_reply> discover_unicast(const std::string& uuid, const std::vector<std::string>& target_ips, const std::function<bool()>& should_continue = {});

// Whether an HTTP redirect's Location stays on original_host: a path, or an
// absolute URL to the same host (any port). ONVIF calls refuse any other
// redirect rather than send their credentials to another host.
R_API bool redirect_stays_on_host(const std::string& original_host, const std::string& location);

struct discovered_info
{
    std::string host;
    int port;
    std::string protocol;
    std::string uri;
    std::string address;
    std::string camera_name;
};

// The cameras in a set of ProbeMatch replies. Only service addresses (XAddrs)
// on the host that sent the reply are used: a reply naming another host's
// services is dropped, so a device can't direct revere's calls (and a camera's
// credentials) to an address other than its own.
R_API std::vector<discovered_info> filter_discovered(const std::vector<discovery_reply>& discovered);

// Where a camera's ONVIF device service answers. Revere stores only the
// service's path (xaddrs), and the port varies: 80 on many cameras, 8000 on
// Reolinks, 2020 on Tapos. Tries, each with an unauthenticated
// GetSystemDateAndTime and a short timeout: hint_port (when set), then unicast
// WS-Discovery to the host, then 80, 8000, 8080 and 2020 over http and 443 over
// https. Null if none answers.
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

// What GetDeviceInformation reports, so revere can show make/model/firmware.
// Any field may be empty if the camera omits it.
struct onvif_device_information
{
    std::string manufacturer;
    std::string model;
    std::string firmware_version;
    std::string serial_number;
    std::string hardware_id;
};

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

// A PTZ preset (a saved pan/tilt/zoom position). token is what GotoPreset needs;
// name is the camera's human label (may be empty).
struct onvif_ptz_preset
{
    std::string token;
    std::string name;
};
// A Media1 GetProfilesResponse: one onvif_profile_info per Profiles, in the camera's order. Throws
// std::runtime_error when the response isn't XML. The camera's strings are bounded: a profile whose token is empty,
// longer than ONVIF_TOKEN_MAX_CHARS or has a control character is left out; a source token like that reads as
// empty; encoding and h264_profile are cut to ONVIF_ENUM_MAX_BYTES. Whitespace around element text is dropped.
constexpr size_t ONVIF_TOKEN_MAX_CHARS = 64;  // ONVIF's ReferenceToken
constexpr size_t ONVIF_ENUM_MAX_BYTES = 16;
constexpr size_t ONVIF_NAME_MAX_CHARS = 64;  // ONVIF's Name
R_API std::vector<onvif_profile_info> parse_profiles_response(const std::string& xml);

// A profile's VideoSourceConfiguration (ONVIF Media), as SetVideoSourceConfiguration needs it back, and its
// rotation (Extension/Rotate: Mode "ON", "OFF", "AUTO", and Degree; each empty when the camera says none).
struct onvif_video_source_config
{
    std::string token;
    std::string name;
    int use_count {0};
    std::string source_token;
    int x {0}, y {0}, width {0}, height {0};
    std::string rotate_mode;
    std::string rotate_degree;
};

// The profile's VideoSourceConfiguration from a GetProfile response; throws when it has none.
R_API onvif_video_source_config parse_video_source_config(const std::string& get_profile_xml);

// Whether the source is turned 180 degrees: Rotate Mode ON with no Degree (which means 180) or Degree 180. A
// camera turned 90 or 270 degrees isn't flipped.
R_API bool is_flipped(const onvif_video_source_config& config);

// What a GetVideoSourceConfigurationOptions response offers for Rotate (Extension/Rotate): its modes, as given,
// and whether the camera restarts to apply a change (@Reboot).
struct onvif_rotate_options
{
    std::vector<std::string> modes;
    bool reboot {false};
};
R_API onvif_rotate_options parse_rotate_options(const std::string& options_xml);

// SetVideoSourceConfiguration's Extension for a flip, appended to config: the camera's own from the GetProfile
// response, kept whole (Rotate's Degree and Mirror, LensDescription, SceneOrientation and anything else), with only
// Rotate/Mode set, and a Degree set to 180 when the flip is turned on. One with no Extension or Rotate gets them.
// A namespace prefix of the camera's that the request doesn't bind the same way is declared where it's used.
R_API void append_flip_extension(pugi::xml_node config, const std::string& get_profile_xml, bool on);

// "Flip picture": the video source turned 180 degrees, for a camera mounted upside down, through the Media
// service's Rotate. ONVIF's Imaging service has no flip; Rotate's Mirror (newer schemas) is kept but not used.
// supported when the source's options offer Rotate ON; reboot when the camera restarts to apply a change.
struct onvif_video_flip
{
    bool supported {false};
    bool on {false};
    bool reboot {false};
};

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

// Which axes a camera's PTZ node can actually drive with ContinuousMove, read
// from its SupportedPTZSpaces (GetNodes). A camera may advertise a PTZ service
// yet have no zoom axis -- a continuous zoom command is then a silent no-op, so
// a UI can hide zoom controls when zoom is known to be false. known is false
// when the reply named no SupportedPTZSpaces at all: then the axes are unknown,
// not absent, and pan_tilt and zoom mean nothing.
struct onvif_ptz_caps
{
    bool known {false};      // some PTZ node listed its SupportedPTZSpaces
    bool pan_tilt {false};   // has a ContinuousPanTiltVelocitySpace
    bool zoom {false};       // has a ContinuousZoomVelocitySpace
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
    int granted_seconds {0};                        // TerminationTime minus CurrentTime, both the camera's own clock; 0 if it didn't say
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

// A Notify a camera pushed (WS-BaseNotification): the same NotificationMessages as a pull, one onvif_event each. It is
// untrusted input: more than MAX_NOTIFY_BYTES, or more than MAX_NOTIFY_EVENTS messages, throws, as does a body that isn't XML.
constexpr size_t MAX_NOTIFY_BYTES = 64 * 1024;
constexpr size_t MAX_NOTIFY_EVENTS = 200;
R_API std::vector<onvif_event> parse_notify(const std::string& xml);

// SubscribeResponse: the subscription's manager address, ReferenceParameters and termination time (the shape of a pull
// point's).
R_API onvif_pull_point parse_subscribe_response(const std::string& xml);

// Whether the camera offers a pull point: the Events capability's WSPullPointSupport. 1 yes, 0 no, -1 it doesn't say.
R_API int parse_ws_pull_point_support(const std::string& capabilities_xml);

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

// Every tptz:Preset in a GetPresetsResponse (token from the @token attribute,
// name from the tt:Name child). Separate so it can be tested without a camera.
R_API std::vector<onvif_ptz_preset> parse_ptz_presets(const std::string& xml);

// The camera's clock, as GetSystemDateAndTime reports it.
struct onvif_system_date_time
{
    std::string date_time_type;   // "Manual" or "NTP"; empty when the camera doesn't say
    bool daylight_savings {false};
    std::string tz;               // POSIX TZ string (TimeZone/TZ); empty when the camera gives none
    time_t time {0};              // UTC: from UTCDateTime, or LocalDateTime and the zone when that's all there is
};

// A GetSystemDateAndTimeResponse. Throws std::runtime_error when it isn't XML or has no SystemDateAndTime.
R_API onvif_system_date_time parse_system_date_and_time(const std::string& xml);

// A tds:SetSystemDateAndTime request, appended to body: a Manual set to utc, keeping the camera's own
// DaylightSavings and TimeZone (current, as read just before), so only its clock changes.
R_API void append_set_system_date_and_time(pugi::xml_node body, time_t utc, const onvif_system_date_time& current);

// The outcome of r_onvif_cam::sync_system_date_and_time(). Offsets are the camera's clock minus this computer's,
// in seconds: positive when the camera is ahead.
struct onvif_time_sync
{
    int64_t offset_before {0};
    int64_t offset_after {0};
    bool was_ntp {false};         // the camera was set to NTP (a set switches it to Manual)
    bool changed {false};         // false: the clock was already within the tolerance, so nothing was sent
};

// How far the camera's clock may be from this computer's after a sync and still count as set: a set takes a
// round trip or two, and the clock has only whole seconds.
constexpr int64_t ONVIF_TIME_SYNC_TOLERANCE_SECONDS = 5;

// Supported continuous-move axes from a GetNodesResponse (any PTZNode's
// SupportedPTZSpaces). Separate so it can be tested without a camera.
R_API onvif_ptz_caps parse_ptz_caps(const std::string& xml);

// The same from a GetConfigurationsResponse, for a camera whose GetNodes says nothing usable: a PTZConfiguration's
// default continuous velocity spaces and its pan/tilt and zoom limits. known is true when the reply held any
// PTZConfiguration; the axes are those some configuration drives.
R_API onvif_ptz_caps parse_ptz_configuration_caps(const std::string& xml);

// Whether a PTZ node can be driven at all: it lists a continuous pan/tilt or zoom space.
inline bool ptz_caps_usable(const onvif_ptz_caps& caps) { return caps.known && (caps.pan_tilt || caps.zoom); }

// How a PTZ probe call (GetNodes, GetConfigurations) went.
enum class ptz_probe_how
{
    answered,      // a good reply (its caps may still say nothing)
    unsupported,   // the camera said it doesn't support the action (is_unsupported_action)
    failed         // anything else: a timeout, a refused login, a camera that is down. Says nothing about PTZ.
};

struct ptz_probe_result
{
    ptz_probe_how how {ptz_probe_how::failed};
    onvif_ptz_caps caps;
};

// Whether a camera that advertises a PTZ service really has PTZ.
enum class ptz_verdict { usable, none, unknown };

// usable: some probe found a node (or configuration) with a pan/tilt or zoom axis. none: a probe positively listed its
// axes and there are none (a node's SupportedPTZSpaces without a continuous space, a configuration naming only axes it
// does not drive), or the camera said twice that it doesn't support the question. unknown: anything else (a reply that
// says nothing about axes, a probe that failed): the camera is treated as before and asked again, not remembered. A real
// PTZ camera must not lose its controls because it answers in a way that isn't read.
R_API ptz_verdict ptz_verdict_of(const ptz_probe_result& nodes, const ptz_probe_result& configurations);

// Whether the text of a failed ONVIF call (r_onvif_cam's errors carry the camera's own reason) says the camera
// definitely doesn't support the action: the subcode "ter:ActionNotSupported" or "ter:OptionalActionNotImplemented",
// or the reason text "Action not supported" / "Action Not Implemented", in any case (cameras send either, or both). Not a timeout, not an authentication failure, not a bad argument.
R_API bool is_unsupported_action(const std::string& what);

// Rebooting a camera (ONVIF SystemReboot), always with its stored credentials.
//
// ONVIF Core (5.9.4, Table 7, the default access policy) lets only an Administrator call SystemReboot (access class
// UNRECOVERABLE) or GetUsers (READ_SYSTEM_SECRET). So a GetUsers that works with the stored credentials says they may
// reboot the camera, without rebooting it. The reply lists the camera's accounts: only its status is read, and nothing
// of it is kept or logged. Some firmware accepts a SystemReboot with no credentials at all; that never counts here, and
// Revere never sends one without them.

// supported: the stored credentials have the rights (GetUsers worked), or a SystemReboot with them worked.
// not_supported: there are no stored credentials, the camera turned the stored credentials away for reboot (they work for
// other calls), or it said it doesn't do SystemReboot.
// unknown: anything else (no answer, credentials it rejects outright, a camera that doesn't do GetUsers, an error).
// Unknown is asked again later.
enum class reboot_support { supported, not_supported, unknown };
R_API const char* reboot_support_name(reboot_support s);   // "supported", "not_supported", "unknown"

struct reboot_check
{
    reboot_support support {reboot_support::unknown};
    // Why, one word: admin, rebooted (supported); no_credentials, not_admin, refused, unsupported (not_supported);
    // credentials_rejected, no_user_list, no_answer, error (unknown).
    std::string reason;
};

// How a camera answered one ONVIF call: it worked; it turned the credentials away (HTTP 401, or a fault saying
// NotAuthorized and the like); it said it doesn't do the action (is_unsupported_action); anything else.
enum class onvif_answer { ok, refused_auth, unsupported, failed };
R_API onvif_answer onvif_answer_of(int status, const std::string& body);

// The verdict from the admin probe. `users` is GetUsers' answer; `device_info` is GetDeviceInformation's, asked only
// when GetUsers turned the credentials away, to tell credentials that work but lack the rights (not_admin) from ones
// the camera rejects outright (credentials_rejected: a changed password, say, which is asked again later).
R_API reboot_check reboot_check_of(onvif_answer users, r_utils::r_nullable<onvif_answer> device_info = r_utils::r_nullable<onvif_answer>());

// The verdict from a SystemReboot's answer.
R_API reboot_check reboot_check_of_reboot(onvif_answer reboot);

// What a SystemReboot did. `message` is the camera's own (e.g. "Rebooting in 30 seconds"), at most REBOOT_MAX_MESSAGE
// bytes. `sent` is false when nothing went out (no stored credentials).
struct reboot_result
{
    reboot_check check;
    bool sent {false};
    std::string message;
};
constexpr size_t REBOOT_MAX_MESSAGE = 200;
// SystemReboot's own timeout. It is never retried after no answer: ONVIF has the camera answer before it reboots, but
// one that goes down first must not get a second request. (Only a camera that refused it, for a SOAP version it doesn't
// take, is asked again in the other one; the clock check every r_onvif_cam starts with normally settles the version.)
constexpr int REBOOT_TIMEOUT_MS = 10000;

// How long an ONVIF events subscription waits after a failed attempt. The wait doubles from 10 s to 10 minutes; a camera
// that said the action isn't supported waits EVENTS_UNSUPPORTED_WAIT_S (6 hours) instead, since asking again every few
// minutes only gets the same answer (a firmware update might add it).
constexpr int EVENTS_BACKOFF_MAX_S = 600;
constexpr int EVENTS_UNSUPPORTED_WAIT_S = 6 * 60 * 60;
R_API int events_wait_s(int backoff_s, bool unsupported);
// Whether a failed events call says the camera doesn't do events: ActionNotSupported (is_unsupported_action) to one of the
// calls that set a subscription up, GetCapabilities, CreatePullPointSubscription or Subscribe (`stage` as r_onvif_events
// names it).
R_API bool events_unsupported(const std::string& stage, const std::string& what);
R_API int events_next_backoff_s(int backoff_s);

enum class soap_version
{
    soap_1_2,   // SOAP 1.2 (default, modern)
    soap_1_1,   // SOAP 1.1 (legacy fallback)
    unknown     // Not yet determined
};

enum class auth_mode
{
    digest,     // PasswordDigest (SHA-1, default)
    text,       // PasswordText (plaintext): only when a caller asks for it; never a fallback
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

    // GetSystemDateAndTime with the session's credentials, parsed (parse_system_date_and_time).
    R_API onvif_system_date_time get_system_date_and_time();

    // Set the camera's clock to this computer's (SetSystemDateAndTime, Manual, its time zone and daylight savings
    // kept), then read it back. A clock already within ONVIF_TIME_SYNC_TOLERANCE_SECONDS is left alone. Needs an admin account on most cameras. Throws std::runtime_error when the camera
    // refuses the set, or when the time read back is still more than ONVIF_TIME_SYNC_TOLERANCE_SECONDS off (a
    // camera that takes the request but keeps its own clock, often one set to sync from its own NTP server).
    // The session's clock offset, used for WS-Security digests, is updated to the camera's new clock.
    R_API onvif_time_sync sync_system_date_and_time();

    R_API onvif_capabilities get_camera_capabilities();

    // GetDeviceInformation (device service): manufacturer, model, firmware
    // version, serial number, hardware id. Throws on a non-200 / SOAP fault.
    R_API onvif_device_information get_device_information();

    // Whether the stored credentials may reboot the camera (see reboot_support), without rebooting it: GetUsers, then
    // GetDeviceInformation if GetUsers turned them away. Nothing is sent without stored credentials. Doesn't throw: a
    // camera that doesn't answer is unknown (no_answer).
    R_API reboot_check check_reboot_support();

    // SystemReboot with the stored credentials, sent once (REBOOT_TIMEOUT_MS). Nothing is sent without them. Doesn't
    // throw: no answer is unknown (no_answer), and the camera may still be rebooting.
    R_API reboot_result system_reboot();

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

    // PTZ. The PTZ service's XAddr from GetCapabilities, null if the camera has
    // none. The rest act on the PTZ service; each throws on a SOAP fault.
    R_API r_utils::r_nullable<std::string> get_ptz_service(const onvif_capabilities& capabilities) const;

    // GetPresets for a media profile (the saved positions the camera holds).
    R_API std::vector<onvif_ptz_preset> get_ptz_presets(const std::string& ptz_service, const onvif_profile_token& profile_token);

    // GetNodes -> which continuous-move axes the camera's PTZ node supports.
    // Used to decide whether to offer zoom controls. Throws on a SOAP fault; a
    // camera that omits SupportedPTZSpaces answers with known false.
    R_API onvif_ptz_caps get_ptz_caps(const std::string& ptz_service);

    // GetConfigurations -> the same, from the PTZ configurations (parse_ptz_configuration_caps). Throws on a SOAP fault.
    R_API onvif_ptz_caps get_ptz_configuration_caps(const std::string& ptz_service);

    // ContinuousMove: pan/tilt/zoom velocities, each normalized to [-1,1] (0 =
    // no motion on that axis, and that axis isn't sent: PanTilt only when pan or
    // tilt moves, Zoom only when zoom does). The camera keeps moving until Stop or, if given,
    // the timeout elapses (an xs:duration such as "PT1S"; empty = none sent).
    R_API void ptz_continuous_move(const std::string& ptz_service, const onvif_profile_token& profile_token,
                                   float pan, float tilt, float zoom, const std::string& timeout = "");

    // Stop PanTilt and/or Zoom motion. PanTilt is always named (true when it
    // stops); stopping both leaves Zoom out (ONVIF: an absent flag stops that
    // axis too), so a camera without zoom never sees a Zoom flag.
    R_API void ptz_stop(const std::string& ptz_service, const onvif_profile_token& profile_token,
                        bool pan_tilt = true, bool zoom = true);

    // The profile's source flip (see onvif_video_flip); a camera that offers no Rotate ON is unsupported, not off.
    R_API onvif_video_flip get_video_flip(onvif_media_service media_service, const onvif_profile_token& profile_token);
    // Turn it on or off (SetVideoSourceConfiguration, ForcePersistence): the source's own configuration, its
    // Extension kept whole (append_flip_extension).
    R_API void set_video_flip(onvif_media_service media_service, const onvif_profile_token& profile_token, bool on);

    // GotoPreset: move to a saved position by its token.
    R_API void ptz_goto_preset(const std::string& ptz_service, const onvif_profile_token& profile_token,
                              const std::string& preset_token);

    // CreatePullPointSubscription with an InitialTerminationTime of termination_seconds.
    R_API onvif_pull_point create_pull_point_subscription(const std::string& events_service, int termination_seconds);

    // Subscribe (WS-BaseNotification) with consumer_url as where the camera POSTs its Notify messages, for
    // termination_seconds. The subscription is renewed and ended with renew() and unsubscribe(), as a pull point's is.
    R_API onvif_pull_point subscribe_push(const std::string& events_service, const std::string& consumer_url, int termination_seconds);

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
