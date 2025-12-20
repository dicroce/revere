
#ifdef IS_WINDOWS
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#endif

#include "r_onvif/r_onvif_cam.h"
#include "r_onvif/r_onvif_session.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_http/r_utils.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_sha1.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_time_utils.h"
#include <chrono>
#include <sstream>
#include <pugixml.hpp>

#ifdef IS_WINDOWS
    #include <time.h>
#endif

using namespace r_onvif;
using namespace r_utils;
using namespace std;
using namespace std::chrono;

// Timeout constants for SOAP operations
static constexpr uint64_t SOAP_TIMEOUT_MS = 30000;          // 30 seconds for normal SOAP calls
static constexpr uint64_t PULL_MESSAGES_TIMEOUT_MS = 10000; // 10 seconds (camera waits 5s, plus network overhead)

// ONVIF namespace constants
static const char* NS_EVENTS = "http://www.onvif.org/ver10/events/wsdl";
static const char* NS_SCHEMA = "http://www.onvif.org/ver10/schema";
static const char* NS_TOPICS = "http://www.onvif.org/ver10/topics";
static const char* NS_WSA = "http://www.w3.org/2005/08/addressing";
static const char* NS_WSNT = "http://docs.oasis-open.org/wsn/b-2";

// Helper to build XPath for local-name matching (namespace agnostic)
static string _xpath_local(const string& local_name)
{
    return "*[local-name()='" + local_name + "']";
}

// HTTP interaction with timeout support
static pair<int, string> _http_request(
    const string& host,
    int port,
    const string& uri,
    const string& body,
    const string& soap_action,
    uint64_t timeout_ms
)
{
    R_LOG_DEBUG("ONVIF event HTTP: %s:%d%s", host.c_str(), port, uri.c_str());

    unique_ptr<r_socket_base> sock;
    if(port == 443)
        sock = make_unique<r_ssl_socket>();
    else
        sock = make_unique<r_socket>();

    sock->connect(host, port);

    r_http::r_client_request request(host, port);
    request.set_method(r_http::method_type("POST"));
    request.set_uri(uri);
    request.set_body(body);

    // SOAP 1.2 headers
    if(!soap_action.empty())
        request.add_header("Content-Type", "application/soap+xml; charset=utf-8; action=\"" + soap_action + "\"");
    else
        request.add_header("Content-Type", "application/soap+xml; charset=utf-8");

    request.add_header("Connection", "close");
    request.add_header("User-Agent", "ONVIF-Client/1.0");

    request.write_request(*sock, timeout_ms);

    r_http::r_client_response response;
    response.read_response(*sock, timeout_ms);

    auto maybe_body = response.get_body_as_string();
    if(maybe_body.is_null())
        return make_pair(response.get_status(), string());

    return make_pair(response.get_status(), maybe_body.value());
}

// Generate a UUID for WS-Addressing MessageID
static string _generate_uuid()
{
    // Simple UUID v4 generator
    unsigned char uuid[16];
    srand((unsigned int)time(NULL) ^ (unsigned int)clock());
    for(int i = 0; i < 16; i++)
        uuid[i] = (unsigned char)rand();

    // Set version (4) and variant (RFC 4122)
    uuid[6] = (uuid[6] & 0x0f) | 0x40;
    uuid[8] = (uuid[8] & 0x3f) | 0x80;

    char buf[48];
    snprintf(buf, sizeof(buf), "urn:uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5], uuid[6], uuid[7],
        uuid[8], uuid[9], uuid[10], uuid[11],
        uuid[12], uuid[13], uuid[14], uuid[15]);
    return string(buf);
}

// Build SOAP envelope with WS-Security authentication and optional WS-Addressing
static string _build_events_soap_envelope(
    const function<void(pugi::xml_node&)>& build_body,
    const r_nullable<string>& username,
    const r_nullable<string>& password,
    int time_offset_seconds,
    const string& wsa_action = "",
    const string& wsa_to = ""
)
{
    pugi::xml_document doc;

    pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";

    pugi::xml_node envelope = doc.append_child("SOAP-ENV:Envelope");
    envelope.append_attribute("xmlns:SOAP-ENV") = "http://www.w3.org/2003/05/soap-envelope";
    envelope.append_attribute("xmlns:wsa") = NS_WSA;
    envelope.append_attribute("xmlns:wsnt") = NS_WSNT;
    envelope.append_attribute("xmlns:tev") = NS_EVENTS;
    envelope.append_attribute("xmlns:tt") = NS_SCHEMA;
    envelope.append_attribute("xmlns:tns1") = NS_TOPICS;

    // Always create header for WS-Security and/or WS-Addressing
    bool need_header = (username && password) || !wsa_action.empty();

    if(need_header)
    {
        envelope.append_attribute("xmlns:wsse") = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd";
        envelope.append_attribute("xmlns:wsu") = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd";

        pugi::xml_node header = envelope.append_child("SOAP-ENV:Header");

        // Add WS-Addressing headers if action is specified
        if(!wsa_action.empty())
        {
            pugi::xml_node action = header.append_child("wsa:Action");
            action.append_attribute("SOAP-ENV:mustUnderstand") = "true";
            action.text().set(wsa_action.c_str());

            pugi::xml_node messageId = header.append_child("wsa:MessageID");
            messageId.text().set(_generate_uuid().c_str());

            pugi::xml_node replyTo = header.append_child("wsa:ReplyTo");
            pugi::xml_node replyAddr = replyTo.append_child("wsa:Address");
            replyAddr.text().set("http://www.w3.org/2005/08/addressing/anonymous");

            if(!wsa_to.empty())
            {
                pugi::xml_node to = header.append_child("wsa:To");
                to.append_attribute("SOAP-ENV:mustUnderstand") = "true";
                to.text().set(wsa_to.c_str());
            }
        }

        // Add WS-Security if credentials provided
        if(username && password)
        {
            pugi::xml_node security = header.append_child("wsse:Security");
            security.append_attribute("SOAP-ENV:mustUnderstand") = "true";

            pugi::xml_node usernameToken = security.append_child("wsse:UsernameToken");

            pugi::xml_node usernameNode = usernameToken.append_child("wsse:Username");
            usernameNode.text().set(username.value().c_str());

            // Generate nonce
            unsigned char nonce_buffer[20];
            srand((unsigned int)time(NULL));
            for(int i = 0; i < 20; i++)
                nonce_buffer[i] = (unsigned char)rand();
            string nonce_b64 = r_string_utils::to_base64(nonce_buffer, 20);

            // Generate timestamp
            auto now = system_clock::now();
            time_t now_t = system_clock::to_time_t(now) + time_offset_seconds;
            struct tm tm_storage;
#ifdef IS_WINDOWS
            gmtime_s(&tm_storage, &now_t);
#else
            gmtime_r(&now_t, &tm_storage);
#endif
            char time_buffer[64];
            strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_storage);

            // Calculate digest: Base64(SHA1(nonce + created + password))
            string digest_input;
            digest_input.append((char*)nonce_buffer, 20);
            digest_input.append(time_buffer);
            digest_input.append(password.value());

            r_sha1 sha1;
            sha1.update((uint8_t*)digest_input.data(), digest_input.size());
            sha1.finalize();
            uint8_t hash[20];
            sha1.get(hash);
            string digest_b64 = r_string_utils::to_base64(hash, 20);

            pugi::xml_node passwordNode = usernameToken.append_child("wsse:Password");
            passwordNode.append_attribute("Type") = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest";
            passwordNode.text().set(digest_b64.c_str());

            pugi::xml_node nonceNode = usernameToken.append_child("wsse:Nonce");
            nonceNode.append_attribute("EncodingType") = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary";
            nonceNode.text().set(nonce_b64.c_str());

            pugi::xml_node createdNode = usernameToken.append_child("wsu:Created");
            createdNode.text().set(time_buffer);
        }
    }

    pugi::xml_node body = envelope.append_child("SOAP-ENV:Body");
    build_body(body);

    ostringstream oss;
    doc.save(oss, "", pugi::format_raw);
    return oss.str();
}

// Parse ISO 8601 duration (PT5M = 5 minutes, PT60S = 60 seconds, etc.)
static seconds _parse_duration(const string& duration)
{
    // Simple parser for common formats: PT5M, PT60S, PT1H, etc.
    if(duration.empty() || duration[0] != 'P')
        return seconds(300); // Default 5 minutes

    int hours = 0, minutes = 0, secs = 0;
    bool in_time = false;
    string num;

    for(size_t i = 1; i < duration.size(); i++)
    {
        char c = duration[i];
        if(c == 'T')
        {
            in_time = true;
        }
        else if(isdigit(c))
        {
            num += c;
        }
        else if(in_time && c == 'H' && !num.empty())
        {
            hours = stoi(num);
            num.clear();
        }
        else if(in_time && c == 'M' && !num.empty())
        {
            minutes = stoi(num);
            num.clear();
        }
        else if(in_time && c == 'S' && !num.empty())
        {
            secs = stoi(num);
            num.clear();
        }
    }

    return seconds(hours * 3600 + minutes * 60 + secs);
}

// Parse ISO 8601 datetime to time_point
static system_clock::time_point _parse_datetime(const string& datetime)
{
    // Format: 2024-12-20T15:30:45Z or 2024-12-20T15:30:45.123Z
    struct tm tm_time = {};
    int ms = 0;

    // Try parsing with milliseconds
    if(sscanf(datetime.c_str(), "%d-%d-%dT%d:%d:%d.%dZ",
              &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday,
              &tm_time.tm_hour, &tm_time.tm_min, &tm_time.tm_sec, &ms) >= 6)
    {
        tm_time.tm_year -= 1900;
        tm_time.tm_mon -= 1;
#ifdef IS_WINDOWS
        time_t t = _mkgmtime(&tm_time);
#else
        time_t t = timegm(&tm_time);
#endif
        return system_clock::from_time_t(t) + milliseconds(ms);
    }

    return system_clock::now();
}

r_onvif_cam::r_onvif_cam(
    const string& host,
    int port,
    const string& protocol,
    const string& xaddrs,
    const r_nullable<string>& username,
    const r_nullable<string>& password
) :
    _host(host),
    _port(port),
    _protocol(protocol),
    _xaddrs(xaddrs),
    _username(username),
    _password(password),
    _events_service_url(),
    _pullpoint_url(),
    _subscription_id(),
    _termination_time(),
    _event_thread(),
    _running(false),
    _motion_cb(),
    _health_mutex(),
    _last_successful_poll(steady_clock::now()),
    _consecutive_errors(0),
    _dead(false),
    _cached_capabilities()
{
}

r_onvif_cam::~r_onvif_cam() noexcept
{
    stop_motion_subscription();
}

event_capability r_onvif_cam::get_event_capabilities()
{
    if(!_cached_capabilities.is_null())
        return _cached_capabilities.value();

    auto caps = _get_event_properties();
    _cached_capabilities.set_value(caps);
    return caps;
}

bool r_onvif_cam::supports_motion_events()
{
    // Retry up to 3 times with exponential backoff - cameras can be slow to respond
    constexpr int MAX_RETRIES = 3;
    int retry_delay_ms = 1000;

    for(int attempt = 1; attempt <= MAX_RETRIES; ++attempt)
    {
        try
        {
            R_LOG_INFO("Checking ONVIF motion event support (attempt %d/%d)...", attempt, MAX_RETRIES);
            auto caps = get_event_capabilities();

            bool has_motion = has_capability(caps, event_capability::motion_alarm);
            bool has_cell = has_capability(caps, event_capability::cell_motion_detector);

            R_LOG_INFO("ONVIF event capabilities: motion_alarm=%d, cell_motion_detector=%d",
                       has_motion ? 1 : 0, has_cell ? 1 : 0);

            return has_motion || has_cell;
        }
        catch(const exception& e)
        {
            R_LOG_WARNING("Failed to query event capabilities (attempt %d/%d): %s",
                          attempt, MAX_RETRIES, e.what());

            if(attempt < MAX_RETRIES)
            {
                R_LOG_INFO("Retrying in %d ms...", retry_delay_ms);
                this_thread::sleep_for(chrono::milliseconds(retry_delay_ms));
                retry_delay_ms *= 2;  // Exponential backoff

                // Clear cached capabilities so we retry the actual query
                _cached_capabilities.clear();
            }
        }
    }

    R_LOG_ERROR("Failed to query ONVIF event capabilities after %d attempts - falling back to software motion", MAX_RETRIES);
    return false;
}

bool r_onvif_cam::start_motion_subscription(motion_event_cb cb)
{
    if(_running)
        return true;

    _motion_cb = cb;
    _running = true;
    _dead = false;
    _consecutive_errors = 0;

    _event_thread = thread(&r_onvif_cam::_event_thread_entry, this);

    // Give the thread a moment to establish subscription
    this_thread::sleep_for(milliseconds(500));

    return !_dead;
}

void r_onvif_cam::stop_motion_subscription()
{
    if(!_running)
        return;

    _running = false;

    if(_event_thread.joinable())
        _event_thread.join();
}

bool r_onvif_cam::dead() const
{
    if(_dead)
        return true;

    lock_guard<mutex> g(_health_mutex);
    auto now = steady_clock::now();
    return (now - _last_successful_poll) > DEAD_TIMEOUT;
}

void r_onvif_cam::_event_thread_entry()
{
    try
    {
        _pullpoint_url = _create_pull_point_subscription();

        {
            lock_guard<mutex> g(_health_mutex);
            _last_successful_poll = steady_clock::now();
            _consecutive_errors = 0;
        }

        R_LOG_INFO("ONVIF event subscription established: %s", _pullpoint_url.c_str());
    }
    catch(const exception& e)
    {
        R_LOG_ERROR("Failed to create ONVIF pull point subscription: %s", e.what());
        _dead = true;
        return;
    }

    while(_running)
    {
        try
        {
            if(_near_termination())
                _renew_subscription();

            _pull_messages();

            {
                lock_guard<mutex> g(_health_mutex);
                _last_successful_poll = steady_clock::now();
                _consecutive_errors = 0;
            }
        }
        catch(const exception& e)
        {
            R_LOG_WARNING("ONVIF event poll failed: %s", e.what());

            lock_guard<mutex> g(_health_mutex);
            _consecutive_errors++;

            if(_consecutive_errors >= MAX_CONSECUTIVE_ERRORS)
            {
                R_LOG_ERROR("ONVIF event subscription dead after %d consecutive errors", _consecutive_errors);
                _dead = true;
                return;
            }

            // Check _running before sleeping
            if(!_running)
                break;

            this_thread::sleep_for(seconds(2));
        }
    }

    try
    {
        _unsubscribe();
    }
    catch(...)
    {
        // Ignore errors during cleanup
    }
}

bool r_onvif_cam::_near_termination() const
{
    auto now = steady_clock::now();
    return (now + minutes(1)) >= _termination_time;
}

string r_onvif_cam::_get_events_service_url()
{
    if(!_events_service_url.empty())
        return _events_service_url;

    // Query device capabilities to get events service URL
    // The r_onvif_cam_caps constructor queries camera time, so we cache the offset
    // to avoid redundant time queries in _get_event_properties and _create_pull_point_subscription
    r_onvif_cam_caps caps(_host, _port, _protocol, _xaddrs, _username, _password);

    // Cache the time offset from the caps object (already computed in its constructor)
    _time_offset = caps.get_time_offset_seconds();
    R_LOG_INFO("Camera time offset: %d seconds", _time_offset);

    auto capabilities_xml = caps.get_camera_capabilities();

    pugi::xml_document doc;
    if(!doc.load_string(capabilities_xml.c_str()))
        R_THROW(("Failed to parse capabilities XML"));

    // Look for Events XAddr
    string xpath = "//" + _xpath_local("Events") + "//" + _xpath_local("XAddr");
    pugi::xpath_node node = doc.select_node(xpath.c_str());

    if(!node)
    {
        // Try alternate path
        xpath = "//" + _xpath_local("Capabilities") + "//" + _xpath_local("Events") + "//" + _xpath_local("XAddr");
        node = doc.select_node(xpath.c_str());
    }

    if(!node)
        R_THROW(("Camera does not advertise Events service"));

    _events_service_url = node.node().child_value();

    R_LOG_INFO("ONVIF Events service URL: %s", _events_service_url.c_str());

    return _events_service_url;
}

event_capability r_onvif_cam::_get_event_properties()
{
    // _get_events_service_url() caches the time offset, so call it first
    string events_url = _get_events_service_url();

    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(events_url, host, port, protocol, uri);

    // Use cached time offset (set by _get_events_service_url)
    string body = _build_events_soap_envelope(
        [](pugi::xml_node& body_node) {
            body_node.append_child("tev:GetEventProperties");
        },
        _username, _password, _time_offset
    );

    auto result = _http_request(host, port, uri, body,
        "http://www.onvif.org/ver10/events/wsdl/EventPortType/GetEventPropertiesRequest",
        SOAP_TIMEOUT_MS);

    if(result.first != 200)
        R_THROW(("GetEventProperties failed with status %d", result.first));

    // Parse response to find supported topics
    pugi::xml_document doc;
    if(!doc.load_string(result.second.c_str()))
        R_THROW(("Failed to parse GetEventProperties response"));

    event_capability caps_result = event_capability::none;

    // Look for motion-related topics in TopicSet
    // Topics are typically: tns1:VideoSource/MotionAlarm, tns1:RuleEngine/CellMotionDetector/Motion
    string response_lower = result.second;
    transform(response_lower.begin(), response_lower.end(), response_lower.begin(), ::tolower);

    if(response_lower.find("motionalarm") != string::npos ||
       response_lower.find("videosource") != string::npos)
    {
        caps_result = caps_result | event_capability::motion_alarm;
    }

    if(response_lower.find("cellmotiondetector") != string::npos ||
       response_lower.find("ruleengine") != string::npos)
    {
        caps_result = caps_result | event_capability::cell_motion_detector;
    }

    R_LOG_INFO("ONVIF event capabilities: motion_alarm=%d, cell_motion=%d",
               has_capability(caps_result, event_capability::motion_alarm),
               has_capability(caps_result, event_capability::cell_motion_detector));

    return caps_result;
}

string r_onvif_cam::_create_pull_point_subscription()
{
    // _get_events_service_url() caches the time offset, so call it first
    string events_url = _get_events_service_url();

    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(events_url, host, port, protocol, uri);

    // Use cached time offset (set by _get_events_service_url)
    string body = _build_events_soap_envelope(
        [](pugi::xml_node& body_node) {
            pugi::xml_node create = body_node.append_child("tev:CreatePullPointSubscription");

            // Request 5 minute initial termination time
            pugi::xml_node termTime = create.append_child("tev:InitialTerminationTime");
            termTime.text().set("PT5M");
        },
        _username, _password, _time_offset
    );

    auto result = _http_request(host, port, uri, body,
        "http://www.onvif.org/ver10/events/wsdl/EventPortType/CreatePullPointSubscriptionRequest",
        SOAP_TIMEOUT_MS);

    if(result.first != 200)
        R_THROW(("CreatePullPointSubscription failed with status %d", result.first));

    // Parse response
    pugi::xml_document doc;
    if(!doc.load_string(result.second.c_str()))
        R_THROW(("Failed to parse CreatePullPointSubscription response"));

    // Get subscription reference address
    string xpath = "//" + _xpath_local("SubscriptionReference") + "//" + _xpath_local("Address");
    pugi::xpath_node addr_node = doc.select_node(xpath.c_str());

    if(!addr_node)
        R_THROW(("CreatePullPointSubscription response missing SubscriptionReference/Address"));

    string pullpoint_addr = addr_node.node().child_value();

    // Get termination time
    xpath = "//" + _xpath_local("CurrentTime");
    pugi::xpath_node current_time_node = doc.select_node(xpath.c_str());

    xpath = "//" + _xpath_local("TerminationTime");
    pugi::xpath_node term_time_node = doc.select_node(xpath.c_str());

    if(term_time_node)
    {
        string term_time_str = term_time_node.node().child_value();
        auto term_tp = _parse_datetime(term_time_str);
        _termination_time = steady_clock::now() + duration_cast<steady_clock::duration>(term_tp - system_clock::now());
    }
    else
    {
        // Default to 5 minutes from now
        _termination_time = steady_clock::now() + minutes(5);
    }

    return pullpoint_addr;
}

void r_onvif_cam::_pull_messages()
{
    if(_pullpoint_url.empty())
        R_THROW(("No pullpoint URL - subscription not established"));

    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(_pullpoint_url, host, port, protocol, uri);

    // Use cached time offset (calculated once at subscription creation)
    // WS-Addressing headers disabled for testing - some cameras may require them
    string body = _build_events_soap_envelope(
        [](pugi::xml_node& body_node) {
            pugi::xml_node pull = body_node.append_child("tev:PullMessages");

            // Timeout - how long camera should wait for events before responding
            // Using PT5S so shutdown doesn't block too long waiting for this to complete
            pugi::xml_node timeout = pull.append_child("tev:Timeout");
            timeout.text().set("PT5S");

            // Message limit
            pugi::xml_node limit = pull.append_child("tev:MessageLimit");
            limit.text().set("10");
        },
        _username, _password, _time_offset,
        "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest",
        _pullpoint_url
    );

    R_LOG_DEBUG("ONVIF PullMessages request to %s:%d%s", host.c_str(), port, uri.c_str());

    auto result = _http_request(host, port, uri, body,
        "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest",
        PULL_MESSAGES_TIMEOUT_MS);

    // Check for shutdown request
    if(!_running)
        return;

    if(result.first != 200)
    {
        R_LOG_ERROR("PullMessages failed - status: %d, response: %s", result.first, result.second.c_str());
        R_THROW(("PullMessages failed with status %d", result.first));
    }

    // Parse response for notification messages
    pugi::xml_document doc;
    if(!doc.load_string(result.second.c_str()))
        R_THROW(("Failed to parse PullMessages response"));

    // Update termination time from response
    string xpath = "//" + _xpath_local("TerminationTime");
    pugi::xpath_node term_node = doc.select_node(xpath.c_str());
    if(term_node)
    {
        string term_time_str = term_node.node().child_value();
        auto term_tp = _parse_datetime(term_time_str);
        _termination_time = steady_clock::now() + duration_cast<steady_clock::duration>(term_tp - system_clock::now());
    }

    // Find all NotificationMessage elements
    xpath = "//" + _xpath_local("NotificationMessage");
    pugi::xpath_node_set messages = doc.select_nodes(xpath.c_str());

    for(auto& msg : messages)
    {
        bool motion_detected = false;
        int64_t timestamp = 0;

        ostringstream oss;
        msg.node().print(oss);

        if(_parse_motion_state(oss.str(), motion_detected, timestamp))
        {
            if(_motion_cb)
                _motion_cb(motion_detected, timestamp);
        }
    }
}

void r_onvif_cam::_renew_subscription()
{
    if(_pullpoint_url.empty())
        return;

    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(_pullpoint_url, host, port, protocol, uri);

    string body = _build_events_soap_envelope(
        [](pugi::xml_node& body_node) {
            pugi::xml_node renew = body_node.append_child("wsnt:Renew");
            pugi::xml_node termTime = renew.append_child("wsnt:TerminationTime");
            termTime.text().set("PT5M");
        },
        _username, _password, _time_offset,
        "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/RenewRequest",
        _pullpoint_url
    );

    auto result = _http_request(host, port, uri, body,
        "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/RenewRequest",
        SOAP_TIMEOUT_MS);

    if(result.first != 200)
        R_THROW(("Renew failed with status %d", result.first));

    // Parse new termination time
    pugi::xml_document doc;
    if(doc.load_string(result.second.c_str()))
    {
        string xpath = "//" + _xpath_local("TerminationTime");
        pugi::xpath_node term_node = doc.select_node(xpath.c_str());
        if(term_node)
        {
            string term_time_str = term_node.node().child_value();
            auto term_tp = _parse_datetime(term_time_str);
            _termination_time = steady_clock::now() + duration_cast<steady_clock::duration>(term_tp - system_clock::now());
        }
        else
        {
            _termination_time = steady_clock::now() + minutes(5);
        }
    }

    R_LOG_DEBUG("ONVIF subscription renewed");
}

void r_onvif_cam::_unsubscribe()
{
    if(_pullpoint_url.empty())
        return;

    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(_pullpoint_url, host, port, protocol, uri);

    try
    {
        string body = _build_events_soap_envelope(
            [](pugi::xml_node& body_node) {
                body_node.append_child("wsnt:Unsubscribe");
            },
            _username, _password, _time_offset,
            "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeRequest",
            _pullpoint_url
        );

        _http_request(host, port, uri, body,
            "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeRequest",
            SOAP_TIMEOUT_MS);

        R_LOG_INFO("ONVIF subscription unsubscribed");
    }
    catch(...)
    {
        // Ignore errors during cleanup
    }

    _pullpoint_url.clear();
}

bool r_onvif_cam::_parse_motion_state(const string& notification_xml, bool& motion_detected, int64_t& timestamp_out)
{
    pugi::xml_document doc;
    if(!doc.load_string(notification_xml.c_str()))
        return false;

    // Check if this is a motion-related event by looking at the Topic
    string xpath = "//" + _xpath_local("Topic");
    pugi::xpath_node topic_node = doc.select_node(xpath.c_str());

    if(!topic_node)
        return false;

    string topic = topic_node.node().child_value();
    string topic_lower = topic;
    transform(topic_lower.begin(), topic_lower.end(), topic_lower.begin(), ::tolower);

    // Check for motion-related topics
    bool is_motion_event = (topic_lower.find("motionalarm") != string::npos ||
                            topic_lower.find("cellmotiondetector") != string::npos ||
                            topic_lower.find("motion") != string::npos);

    if(!is_motion_event)
        return false;

    // Get the State value from Data/SimpleItem[@Name="State"]
    xpath = "//" + _xpath_local("Data") + "//" + _xpath_local("SimpleItem");
    pugi::xpath_node_set items = doc.select_nodes(xpath.c_str());

    for(auto& item : items)
    {
        string name = item.node().attribute("Name").value();
        if(name == "State" || name == "IsMotion")
        {
            string value = item.node().attribute("Value").value();
            motion_detected = (value == "true" || value == "1");
            break;
        }
    }

    // Get timestamp from Message[@UtcTime]
    xpath = "//" + _xpath_local("Message");
    pugi::xpath_node msg_node = doc.select_node(xpath.c_str());
    if(msg_node)
    {
        string utc_time = msg_node.node().attribute("UtcTime").value();
        if(!utc_time.empty())
        {
            auto tp = _parse_datetime(utc_time);
            timestamp_out = duration_cast<milliseconds>(tp.time_since_epoch()).count();
        }
        else
        {
            timestamp_out = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
        }
    }
    else
    {
        timestamp_out = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    }

    R_LOG_DEBUG("ONVIF motion event: detected=%d, timestamp=%lld", motion_detected, timestamp_out);

    return true;
}
