#ifdef IS_WINDOWS
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#endif

#include <thread>
#include <sstream>
#include <map>
#include <algorithm>
#include <tuple>
#include "r_http/r_client_request.h"
#include "r_http/r_methods.h"
#include "r_utils/r_sha1.h"
#include "r_utils/r_sha256.h"
#include "r_utils/r_string_utils.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>
#include "r_onvif/r_onvif_session.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_ssl_socket.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_sha1.h"
#include "r_utils/r_time_utils.h"
#include "r_utils/r_uuid.h"
#include "r_utils/r_udp_sender.h"
#include "r_utils/r_udp_receiver.h"
#include "r_utils/r_udp_socket.h"
#include "r_utils/r_std_utils.h"
#include "r_http/r_utils.h"
#include "r_http/r_client_response.h"
#include "r_http/r_status_codes.h"
#include "r_http/r_utils.h"

#ifdef IS_WINDOWS
    #include <ws2tcpip.h>
    #include <winsock2.h>
    #include <wincrypt.h>
    #include <iphlpapi.h>
    #include <io.h>
    #include <fcntl.h>
    #include <stdio.h>
    #include <time.h>
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <ifaddrs.h>
    #include <sys/ioctl.h>
    #include <sys/types.h>
    #include <net/if.h>
    #include <netinet/in.h>
    #include <sys/time.h>
#endif

#include <pugixml.hpp>


using namespace r_onvif;
using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace std;

static const int MAX_REDIRECTS = 5;

// A redirect to another host is refused, not followed: the request body (an
// ONVIF call's UsernameToken among it) would go to that host.
struct redirect_refused : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

bool r_onvif::redirect_stays_on_host(const string& original_host, const string& location)
{
    if (location.find("://") == string::npos)
        return true;  // a path on the same host
    string host, protocol, uri;
    int port = 0;
    try { r_http::parse_url_parts(location, host, port, protocol, uri); }
    catch (const std::exception&) { return false; }
    return !host.empty() && r_string_utils::to_lower(host) == r_string_utils::to_lower(original_host);
}

static pair<int, string> _http_interact(
    string host,
    int port,
    string http_method,
    string uri,
    string body,
    soap_version soap_ver = soap_version::unknown,
    const string& soap_action = "",
    int timeout_ms = 30000,
    int net_attempts = 2,
    bool log_request = true   // false for requests made every few seconds (event pulls); failures still log
)
{
    const int MAX_NET_ATTEMPTS = net_attempts;
    int redirect_count = 0;

retry_redirect:
    if (redirect_count >= MAX_REDIRECTS)
        throw std::runtime_error("Too many redirects (max " + std::to_string(MAX_REDIRECTS) + ")");

    for (int net_attempt = 0; net_attempt < MAX_NET_ATTEMPTS; ++net_attempt)
    {
        if (net_attempt > 0)
        {
            R_LOG_INFO("HTTP retry %d/%d after 300ms...", net_attempt + 1, MAX_NET_ATTEMPTS);
            this_thread::sleep_for(chrono::milliseconds(300));
        }

        try
        {
            if (log_request)
                R_LOG_INFO("HTTP interact: %s(%d)(%s)", host.c_str(), port, uri.c_str());

            std::unique_ptr<r_utils::r_socket_base> sock;
            if (port == 443)
                sock = std::make_unique<r_utils::r_ssl_socket>();
            else
                sock = std::make_unique<r_utils::r_socket>();

            sock->connect(host, port);

            r_http::r_client_request request(host, port);
            request.set_method(r_http::method_type(http_method));
            request.set_uri(uri);
            request.set_body(body);

            if (soap_ver == soap_version::soap_1_2)
            {
                if (!soap_action.empty())
                    request.add_header("Content-Type", "application/soap+xml; charset=utf-8; action=\"" + soap_action + "\"");
                else
                    request.add_header("Content-Type", "application/soap+xml; charset=utf-8");
            }
            else if (soap_ver == soap_version::soap_1_1)
            {
                request.add_header("Content-Type", "text/xml; charset=utf-8");
                if (!soap_action.empty())
                    request.add_header("SOAPAction", "\"" + soap_action + "\"");
            }

            request.add_header("Connection", "close");
            request.add_header("User-Agent", "ONVIF-Client/1.0");

            request.write_request(*sock, timeout_ms);

            r_http::r_client_response response;
            response.read_response(*sock, timeout_ms);

            if(response.get_status() == 302 || response.get_status() == 301 || response.get_status() == 307)
            {
                string location = response.get_header("Location");
                R_LOG_INFO("Redirect response to %s", location.c_str());
                if(location.empty())
                    throw std::runtime_error("Redirect response but no Location header");

                if (!redirect_stays_on_host(host, location))
                    throw redirect_refused("Refused a redirect to another host");

                if (location.find("://") != string::npos)
                {
                    string protocol, new_uri;
                    r_http::parse_url_parts(location, host, port, protocol, new_uri);
                    uri = new_uri;
                }
                else if (location[0] == '/')
                    uri = location;
                else
                {
                    size_t last_slash = uri.rfind('/');
                    if (last_slash != string::npos)
                        uri = uri.substr(0, last_slash + 1) + location;
                    else
                        uri = "/" + location;
                }

                R_LOG_INFO("Redirecting to (%s)(%d)(%s)", host.c_str(), port, uri.c_str());
                redirect_count++;
                goto retry_redirect;
            }

            auto maybe_body = response.get_body_as_string();
            if(maybe_body.is_null())
                return make_pair(response.get_status(), string());
            return make_pair(response.get_status(), maybe_body.value());
        }
        catch (const redirect_refused&)
        {
            throw;  // not a network failure: asking again gets the same redirect
        }
        catch (const std::exception& ex)
        {
            if (net_attempt + 1 == MAX_NET_ATTEMPTS)
                throw;
            R_LOG_INFO("HTTP attempt %d/%d failed (%s), retrying...", net_attempt + 1, MAX_NET_ATTEMPTS, ex.what());
        }
    }

    throw std::runtime_error("HTTP interaction failed after retries");
}

static time_t _portable_timegm(struct tm* t)
{
#ifdef IS_WINDOWS
    // Windows implementation
    return _mkgmtime(t); // Windows has _mkgmtime for UTC time
#elif defined(IS_MACOS)
    // macOS has timegm
    return timegm(t);
#else
    // Linux/POSIX implementation
    char* tz = getenv("TZ");
    setenv("TZ", "UTC", 1);
    tzset();
    time_t result = mktime(t);
    if (tz)
        setenv("TZ", tz, 1);
    else unsetenv("TZ");
    tzset();
    return result;
#endif
}

// Forward declarations for namespace-agnostic XPath helpers (defined later)
static string _xpath_local(const string& local_name, const string& ns_uri = "");
static pugi::xpath_node _select_node_local(const pugi::xml_node& parent, const string& local_name, const string& ns_uri = "");
static pugi::xpath_node _select_node_local_abs(const pugi::xml_document& doc, const string& local_name, const string& ns_uri = "");

// Namespace constant - defined here, used throughout
static const char* NS_SCHEMA = "http://www.onvif.org/ver10/schema";

static r_nullable<time_t> _parse_onvif_date_time(const std::string& xmlResponse)
{
    r_nullable<time_t> response;

    pugi::xml_document doc;
    doc.load_string(xmlResponse.c_str());

    // Check for DaylightSavings using namespace-agnostic XPath
    bool daylightSavings = false;
    pugi::xpath_node dstNode = _select_node_local_abs(doc, "DaylightSavings", NS_SCHEMA);
    if (dstNode)
    {
        std::string dstValue = dstNode.node().child_value();
        daylightSavings = (dstValue == "true" || dstValue == "1");
    }

    // Try to get timezone information
    std::string timezone;
    pugi::xpath_node tzNode = _select_node_local_abs(doc, "TZ", NS_SCHEMA);
    if (tzNode)
    {
        timezone = tzNode.node().child_value();
    }

    // First try to get UTCDateTime
    struct tm timeinfo = {};
    bool useUtc = false;

    pugi::xpath_node utcNode = _select_node_local_abs(doc, "UTCDateTime", NS_SCHEMA);
    if (utcNode)
    {
        pugi::xml_node utcElement = utcNode.node();

        // Get year, month, day
        pugi::xpath_node dateNode = _select_node_local(utcElement, "Date", NS_SCHEMA);
        if (dateNode)
        {
            pugi::xml_node dateElement = dateNode.node();

            pugi::xpath_node yearNode = _select_node_local(dateElement, "Year", NS_SCHEMA);
            pugi::xpath_node monthNode = _select_node_local(dateElement, "Month", NS_SCHEMA);
            pugi::xpath_node dayNode = _select_node_local(dateElement, "Day", NS_SCHEMA);

            if (yearNode && monthNode && dayNode)
            {
                timeinfo.tm_year = std::stoi(yearNode.node().child_value()) - 1900;
                timeinfo.tm_mon = std::stoi(monthNode.node().child_value()) - 1;
                timeinfo.tm_mday = std::stoi(dayNode.node().child_value());
            }
        }

        // Get hour, minute, second
        pugi::xpath_node timeNode = _select_node_local(utcElement, "Time", NS_SCHEMA);
        if (timeNode)
        {
            pugi::xml_node timeElement = timeNode.node();

            pugi::xpath_node hourNode = _select_node_local(timeElement, "Hour", NS_SCHEMA);
            pugi::xpath_node minuteNode = _select_node_local(timeElement, "Minute", NS_SCHEMA);
            pugi::xpath_node secondNode = _select_node_local(timeElement, "Second", NS_SCHEMA);

            if (hourNode && minuteNode && secondNode)
            {
                timeinfo.tm_hour = std::stoi(hourNode.node().child_value());
                timeinfo.tm_min = std::stoi(minuteNode.node().child_value());
                timeinfo.tm_sec = std::stoi(secondNode.node().child_value());
            }
        }

        useUtc = true;
    }

    // If no UTCDateTime, try LocalDateTime
    if (!useUtc)
    {
        pugi::xpath_node localNode = _select_node_local_abs(doc, "LocalDateTime", NS_SCHEMA);
        if (localNode)
        {
            pugi::xml_node localElement = localNode.node();

            // Get year, month, day
            pugi::xpath_node dateNode = _select_node_local(localElement, "Date", NS_SCHEMA);
            if (dateNode)
            {
                pugi::xml_node dateElement = dateNode.node();

                pugi::xpath_node yearNode = _select_node_local(dateElement, "Year", NS_SCHEMA);
                pugi::xpath_node monthNode = _select_node_local(dateElement, "Month", NS_SCHEMA);
                pugi::xpath_node dayNode = _select_node_local(dateElement, "Day", NS_SCHEMA);

                if (yearNode && monthNode && dayNode)
                {
                    timeinfo.tm_year = std::stoi(yearNode.node().child_value()) - 1900;
                    timeinfo.tm_mon = std::stoi(monthNode.node().child_value()) - 1;
                    timeinfo.tm_mday = std::stoi(dayNode.node().child_value());
                }
            }

            // Get hour, minute, second
            pugi::xpath_node timeNode = _select_node_local(localElement, "Time", NS_SCHEMA);
            if (timeNode)
            {
                pugi::xml_node timeElement = timeNode.node();

                pugi::xpath_node hourNode = _select_node_local(timeElement, "Hour", NS_SCHEMA);
                pugi::xpath_node minuteNode = _select_node_local(timeElement, "Minute", NS_SCHEMA);
                pugi::xpath_node secondNode = _select_node_local(timeElement, "Second", NS_SCHEMA);

                if (hourNode && minuteNode && secondNode)
                {
                    timeinfo.tm_hour = std::stoi(hourNode.node().child_value());
                    timeinfo.tm_min = std::stoi(minuteNode.node().child_value());
                    timeinfo.tm_sec = std::stoi(secondNode.node().child_value());
                }
            }
        }
    }

    // Convert to time_t
    time_t timestamp = 0;
    
    // If we have local time with timezone info
    if (!useUtc && !timezone.empty())
    {
        // First convert the local time to a time_t (treating it temporarily as UTC)
        time_t localAsUtc = _portable_timegm(&timeinfo);
        
        // Parse timezone in format like "GMT-05:00" or "GMT+01:00"
        int offsetSeconds = 0;
        
        // Skip the "GMT" prefix
        size_t pos = timezone.find("GMT");
        if (pos != std::string::npos)
        {
            std::string offset = timezone.substr(pos + 3);
            
            char sign = offset[0];
            if (sign == '+' || sign == '-')
            {
                // Parse hours and minutes
                size_t colonPos = offset.find(':');
                if (colonPos != std::string::npos)
                {
                    int offsetHours = std::stoi(offset.substr(1, colonPos - 1));
                    int offsetMinutes = std::stoi(offset.substr(colonPos + 1));
                    
                    // Calculate total seconds
                    offsetSeconds = (offsetHours * 3600) + (offsetMinutes * 60);
                    
                    // Apply the sign (note the direction - when converting local to UTC)
                    // For example: GMT-05:00 means UTC is 5 hours ahead of local time
                    // So to convert local to UTC, we ADD 5 hours
                    if (sign == '-')
                        offsetSeconds = offsetSeconds; // Positive - add to local time
                    else
                        offsetSeconds = -offsetSeconds; // Negative - subtract from local time
                }
            }
        }
        
        // Apply DST correction if needed
        if (daylightSavings)
            offsetSeconds -= 3600; // Subtract 1 hour
        
        // Apply both timezone and DST adjustments to get UTC
        timestamp = localAsUtc + offsetSeconds;
    }
    else if (useUtc)
        timestamp = _portable_timegm(&timeinfo); // We already have UTC time, just convert it
    else
    {
        // No timezone info, assume it's in local timezone
        timeinfo.tm_isdst = daylightSavings ? 1 : 0;
        timestamp = mktime(&timeinfo);
    }

    response = timestamp;
    
    return response;
}

// Build namespace-agnostic XPath query using local-name() and namespace-uri()
// Example: _xpath_local("Media", "http://www.onvif.org/ver10/device/wsdl")
//   returns: "*[local-name()='Media' and namespace-uri()='http://www.onvif.org/ver10/device/wsdl']"
static string _xpath_local(const string& local_name, const string& ns_uri)
{
    if (ns_uri.empty())
        return "*[local-name()='" + local_name + "']";
    else
        return "*[local-name()='" + local_name + "' and namespace-uri()='" + ns_uri + "']";
}

// Common ONVIF namespace URIs (NS_SCHEMA defined earlier for use by _parse_onvif_date_time)
static const char* NS_DEVICE = "http://www.onvif.org/ver10/device/wsdl";
static const char* NS_MEDIA = "http://www.onvif.org/ver10/media/wsdl";
static const char* NS_SOAP12 = "http://www.w3.org/2003/05/soap-envelope";
static const char* NS_SOAP11 = "http://schemas.xmlsoap.org/soap/envelope/";

// Helper to select node with namespace-agnostic XPath, with fallback
static pugi::xpath_node _select_node_local(const pugi::xml_node& parent, const string& local_name, const string& ns_uri)
{
    string xpath = ".//" + _xpath_local(local_name, ns_uri);
    pugi::xpath_node node = parent.select_node(xpath.c_str());
    if (!node && !ns_uri.empty())
    {
        // Fallback without namespace constraint
        xpath = ".//" + _xpath_local(local_name, "");
        node = parent.select_node(xpath.c_str());
    }
    return node;
}

static pugi::xpath_node _select_node_local_abs(const pugi::xml_document& doc, const string& local_name, const string& ns_uri)
{
    string xpath = "//" + _xpath_local(local_name, ns_uri);
    pugi::xpath_node node = doc.select_node(xpath.c_str());
    if (!node && !ns_uri.empty())
    {
        // Fallback without namespace constraint
        xpath = "//" + _xpath_local(local_name, "");
        node = doc.select_node(xpath.c_str());
    }
    return node;
}

static r_nullable<string> _get_scope_field(const string& scope, const string& field_name)
{
    r_nullable<string> output;

    auto pos = scope.find(field_name);
    if(pos != string::npos)
    {
        auto space_pos = scope.find(" ", pos);

        auto contents = (space_pos != string::npos)?scope.substr(pos, space_pos - pos):scope.substr(pos);
        auto last_slash = contents.rfind("/");
        auto start = (last_slash == string::npos)?0:last_slash + 1;
        output.set_value(r_string_utils::uri_decode(contents.substr(start)));
    }

    return output;
}

// Helper to check if an IP address is in the link-local range (169.254.0.0/16)
// These addresses are auto-assigned when DHCP fails and are usually not routable
static bool _is_link_local_address(const string& host)
{
    // Check if it starts with "169.254."
    return host.find("169.254.") == 0;
}

// A WS-Discovery probe is one small datagram, so a send that doesn't go out at
// once is not worth waiting for. Without a limit, a sendto held by something
// outside revere (a network filter such as Little Snitch, a stalled interface)
// blocks the discovery thread forever, and r_agent::stop() joins it forever.
// POSIX sends use MSG_DONTWAIT as well; Windows has no per-call flag, so it
// relies on SO_SNDTIMEO alone.
static const int PROBE_SEND_TIMEOUT_MS = 1000;
#ifdef IS_WINDOWS
static void _set_probe_send_timeout(SOCKET sock)
{
    DWORD sendTimeout = PROBE_SEND_TIMEOUT_MS;
    ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendTimeout, sizeof(sendTimeout));
}
#define PROBE_SEND_FLAGS 0
#else
static void _set_probe_send_timeout(int sock)
{
    struct timeval sendTimeout;
    sendTimeout.tv_sec = PROBE_SEND_TIMEOUT_MS / 1000;
    sendTimeout.tv_usec = (PROBE_SEND_TIMEOUT_MS % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));
}
#define PROBE_SEND_FLAGS MSG_DONTWAIT
#endif

// Helper function to discover ONVIF devices on a specific interface
// The IPv4 address a datagram came from ("" if it can't be read).
static string _sender_ip(const struct sockaddr_in& from)
{
    char ip[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip)) == nullptr)
        return string();
    return string(ip);
}

static vector<r_onvif::discovery_reply> _discover_on_interface(const string& interface_ip, const string& broadcast_message, const std::function<bool()>& should_continue)
{
    vector<r_onvif::discovery_reply> discovered;

#ifdef IS_WINDOWS
    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return discovered;

    // Enable address reuse
    BOOL reuse = TRUE;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    // Set receive timeout. Cameras answer a WS-Discovery probe within tens of
    // milliseconds, so 2s of silence is a reliable "everyone has replied" signal.
    DWORD recvTimeout = 2000; // 2 seconds
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recvTimeout, sizeof(recvTimeout));
    _set_probe_send_timeout(sock);

    // Convert interface IP string to IN_ADDR
    IN_ADDR iface_addr;
    inet_pton(AF_INET, interface_ip.c_str(), &iface_addr);

    // Bind to ANY address on a dynamic port
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(0);
    localAddr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr)) != 0) {
        closesocket(sock);
        return discovered;
    }

    // Set multicast interface
    ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&iface_addr, sizeof(iface_addr));

    // Join multicast group on this interface
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    mreq.imr_interface = iface_addr;
    ::setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));

    // Set multicast TTL
    DWORD ttl = 1;
    ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));

    // Multicast destination
    struct sockaddr_in multicastAddr;
    memset(&multicastAddr, 0, sizeof(multicastAddr));
    multicastAddr.sin_family = AF_INET;
    multicastAddr.sin_port = htons(3702);
    multicastAddr.sin_addr.s_addr = inet_addr("239.255.255.250");

    // Send discovery probe 3 times for reliability (multicast UDP can be dropped)
    bool any_sent = false;
    for (int probe = 0; probe < 3; ++probe)
    {
        if (should_continue && !should_continue())
            break;
        if (probe > 0)
            this_thread::sleep_for(chrono::milliseconds(150));
        int bytesSent = ::sendto(sock, broadcast_message.c_str(), (int)broadcast_message.length(), PROBE_SEND_FLAGS,
                                (struct sockaddr*)&multicastAddr, sizeof(multicastAddr));
        if (bytesSent > 0)
            any_sent = true;
    }

    if (!any_sent) {
        closesocket(sock);
        return discovered;
    }

    // Receive responses
    char buf[8192];
    int timeoutCounts = 0;
    while (timeoutCounts < 1) {
        if (should_continue && !should_continue())
            break;
        struct sockaddr_in fromAddr;
        int fromAddrLen = sizeof(fromAddr);
        int len = ::recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&fromAddr, &fromAddrLen);

        if (len < 0) {
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT)
                timeoutCounts++;
            else
                break;
        }
        else if (len > 0) {
            buf[len] = '\0';
            discovered.push_back({_sender_ip(fromAddr), string(buf, len)});
        }
    }

    closesocket(sock);
#endif

#if defined(IS_LINUX) || defined(IS_MACOS)
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return discovered;

    // Enable address reuse
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // Set receive timeout. Cameras answer a WS-Discovery probe within tens of
    // milliseconds, so 2s of silence is a reliable "everyone has replied" signal.
    struct timeval recvTimeout;
    recvTimeout.tv_sec = 2;
    recvTimeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));
    _set_probe_send_timeout(sock);

    // Convert interface IP string to in_addr
    struct in_addr iface_addr;
    inet_pton(AF_INET, interface_ip.c_str(), &iface_addr);

    // Bind to ANY address on a dynamic port
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(0);
    localAddr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
        close(sock);
        return discovered;
    }

    // Set multicast interface
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &iface_addr, sizeof(iface_addr));

    // Join multicast group on this interface
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    mreq.imr_interface = iface_addr;
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    // Set multicast TTL
    int ttl = 1;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // Multicast destination
    struct sockaddr_in multicastAddr;
    memset(&multicastAddr, 0, sizeof(multicastAddr));
    multicastAddr.sin_family = AF_INET;
    multicastAddr.sin_port = htons(3702);
    multicastAddr.sin_addr.s_addr = inet_addr("239.255.255.250");

    // Send discovery probe 3 times for reliability (multicast UDP can be dropped)
    bool any_sent = false;
    for (int probe = 0; probe < 3; ++probe)
    {
        if (should_continue && !should_continue())
            break;
        if (probe > 0)
            this_thread::sleep_for(chrono::milliseconds(150));
        int bytesSent = sendto(sock, broadcast_message.c_str(), broadcast_message.length(), PROBE_SEND_FLAGS,
                              (struct sockaddr*)&multicastAddr, sizeof(multicastAddr));
        if (bytesSent > 0)
            any_sent = true;
    }

    if (!any_sent) {
        close(sock);
        return discovered;
    }

    // Receive responses
    char buf[8192];
    int timeoutCounts = 0;
    while (timeoutCounts < 1) {
        if (should_continue && !should_continue())
            break;
        struct sockaddr_in fromAddr;
        socklen_t fromAddrLen = sizeof(fromAddr);
        int len = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&fromAddr, &fromAddrLen);

        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                timeoutCounts++;
            else
                break;
        }
        else if (len > 0) {
            buf[len] = '\0';
            discovered.push_back({_sender_ip(fromAddr), string(buf, len)});
        }
    }

    close(sock);
#endif

    return discovered;
}

// New function that supports both SHA-256 and SHA-1
// use_sha256: true = use SHA-256, false = use SHA-1 (default for compatibility)
static void _add_username_digest_header_with_algorithm(
    pugi::xml_document* doc,
    pugi::xml_node root,
    const std::string& username,
    const std::string& password,
    int time_offset_seconds,
    bool use_sha256
)
{
    srand((unsigned int)time(NULL));

#ifdef IS_WINDOWS
    _setmode(0, O_BINARY);
#endif

    unsigned int nonce_chunk_size = 20;
    unsigned char nonce_buffer[20];
    char nonce_base64[1024] = {0};
    char time_holder[1024] = {0};
    char digest_base64[1024] = {0};

    for (unsigned int i=0; i<nonce_chunk_size; i++)
        nonce_buffer[i] = (unsigned char)rand();

    unsigned char nonce_result[30];
    memset(nonce_result, 0, 30);

    auto b64_encoded = r_utils::r_string_utils::to_base64(nonce_buffer, nonce_chunk_size);
    memcpy(nonce_result, b64_encoded.c_str(), b64_encoded.length());

#ifdef IS_WINDOWS
    strcpy_s(nonce_base64, 1024, (char*)nonce_result);
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    strcpy(nonce_base64, (char*)nonce_result);
#endif

    auto now = chrono::system_clock::now();
    auto delta = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch());

    struct timeval tv;
    tv.tv_sec = (long)(delta.count() / 1000);
    tv.tv_usec = (delta.count() % 1000) * 1000;

    int millisec = tv.tv_usec / 1000;

    char time_buffer[1024];
    struct tm* this_tm = nullptr;
#ifdef IS_WINDOWS
    struct tm tm_storage;
    time_t then = tv.tv_sec + time_offset_seconds;
    auto err = gmtime_s(&tm_storage, &then);
    if(err != 0)
        R_THROW(("gmtime_s failed"));
    this_tm = &tm_storage;
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    time_t then = tv.tv_sec + time_offset_seconds;
    this_tm = gmtime((time_t*)&then);
#endif
    size_t time_buffer_length = strftime(time_buffer, 1024, "%Y-%m-%dT%H:%M:%S.", this_tm);
    time_buffer[time_buffer_length] = '\0';

    char milli_buf[16] = {0};
#ifdef IS_WINDOWS
    sprintf_s(milli_buf, 16, "%03dZ", millisec);
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    sprintf(milli_buf, "%03dZ", millisec);
#endif
#ifdef IS_WINDOWS
    strcat_s(time_buffer, 1024, milli_buf);
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    strcat(time_buffer, milli_buf);
#endif

    unsigned char hash[32];  // Max size for SHA-256
    unsigned int digest_chunk_size;

    if(use_sha256)
    {
        // Use SHA-256 authentication
        r_sha256 ctx;
        ctx.update(nonce_buffer, nonce_chunk_size);
        ctx.update((const unsigned char*)time_buffer, strlen(time_buffer));
        ctx.update((const unsigned char*)password.c_str(), strlen(password.c_str()));
        ctx.finalize();
        ctx.get(&hash[0]);
        digest_chunk_size = 32;
    }
    else
    {
        // Use SHA-1 authentication (legacy, for compatibility)
        r_sha1 ctx;
        ctx.update(nonce_buffer, nonce_chunk_size);
        ctx.update((const unsigned char*)time_buffer, strlen(time_buffer));
        ctx.update((const unsigned char*)password.c_str(), strlen(password.c_str()));
        ctx.finalize();
        ctx.get(&hash[0]);
        digest_chunk_size = 20;
    }

    unsigned char digest_result[128];
    b64_encoded = r_string_utils::to_base64(&hash[0], digest_chunk_size);
    memset(digest_result, 0, 128);
    memcpy(digest_result, b64_encoded.c_str(), b64_encoded.length());

#ifdef IS_WINDOWS
    strcpy_s(time_holder, 1024, time_buffer);
    strcpy_s(digest_base64, 1024, (char*)digest_result);
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    strcpy(time_holder, time_buffer);
    strcpy(digest_base64, (const char *)digest_result);
#endif

    // Add WSSE and WSU namespaces
    root.append_attribute("xmlns:wsse") = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd";
    root.append_attribute("xmlns:wsu") = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd";

    // Create Header element
    pugi::xml_node header = root.prepend_child("SOAP-ENV:Header");

    // Create Security element
    pugi::xml_node security = header.append_child("wsse:Security");
    security.append_attribute("SOAP-ENV:mustUnderstand") = "1";

    // Create UsernameToken element
    pugi::xml_node usernameToken = security.append_child("wsse:UsernameToken");

    // Create Username element
    pugi::xml_node usernameElem = usernameToken.append_child("wsse:Username");
    usernameElem.text().set(username.c_str());

    // Create Password element
    pugi::xml_node passwordElem = usernameToken.append_child("wsse:Password");
    passwordElem.append_attribute("Type") =
        "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest";
    passwordElem.text().set(digest_base64);

    // Create Nonce element
    pugi::xml_node nonceElem = usernameToken.append_child("wsse:Nonce");
    nonceElem.append_attribute("EncodingType") =
        "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary";
    nonceElem.text().set(nonce_base64);

    // Create Created element
    pugi::xml_node createdElem = usernameToken.append_child("wsu:Created");
    createdElem.text().set(time_holder);
}

// Legacy function - wrapper that uses SHA-1 for backward compatibility
static void _add_username_digest_header(
    pugi::xml_document* doc,
    pugi::xml_node root,
    const std::string& username,
    const std::string& password,
    int time_offset_seconds
)
{
    // Delegate to the new function with SHA-1 (use_sha256 = false)
    _add_username_digest_header_with_algorithm(doc, root, username, password, time_offset_seconds, false);
}

// PasswordText authentication - for cameras with buggy digest implementations
static void _add_username_text_header(
    pugi::xml_document* doc,
    pugi::xml_node root,
    const std::string& username,
    const std::string& password,
    int time_offset_seconds
)
{
    // Generate timestamp
    auto now = chrono::system_clock::now();
    auto delta = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch());

    struct timeval tv;
    tv.tv_sec = (long)(delta.count() / 1000);
    tv.tv_usec = (delta.count() % 1000) * 1000;

    int millisec = tv.tv_usec / 1000;

    char time_buffer[1024];
    struct tm* this_tm = nullptr;
#ifdef IS_WINDOWS
    struct tm tm_storage;
    time_t then = tv.tv_sec + time_offset_seconds;
    auto err = gmtime_s(&tm_storage, &then);
    if(err != 0)
        R_THROW(("gmtime_s failed"));
    this_tm = &tm_storage;
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    time_t then = tv.tv_sec + time_offset_seconds;
    this_tm = gmtime((time_t*)&then);
#endif
    size_t time_buffer_length = strftime(time_buffer, 1024, "%Y-%m-%dT%H:%M:%S.", this_tm);
    time_buffer[time_buffer_length] = '\0';

    char milli_buf[16] = {0};
#ifdef IS_WINDOWS
    sprintf_s(milli_buf, 16, "%03dZ", millisec);
    strcat_s(time_buffer, 1024, milli_buf);
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
    sprintf(milli_buf, "%03dZ", millisec);
    strcat(time_buffer, milli_buf);
#endif

    // Add WSSE and WSU namespaces
    root.append_attribute("xmlns:wsse") = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd";
    root.append_attribute("xmlns:wsu") = "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd";

    // Create Header element
    pugi::xml_node header = root.prepend_child("SOAP-ENV:Header");

    // Create Security element
    pugi::xml_node security = header.append_child("wsse:Security");
    security.append_attribute("SOAP-ENV:mustUnderstand") = "1";

    // Create UsernameToken element
    pugi::xml_node usernameToken = security.append_child("wsse:UsernameToken");

    // Create Username element
    pugi::xml_node usernameElem = usernameToken.append_child("wsse:Username");
    usernameElem.text().set(username.c_str());

    // Create Password element with PasswordText type
    pugi::xml_node passwordElem = usernameToken.append_child("wsse:Password");
    passwordElem.append_attribute("Type") =
        "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordText";
    passwordElem.text().set(password.c_str());

    // Create Created element
    pugi::xml_node createdElem = usernameToken.append_child("wsu:Created");
    createdElem.text().set(time_buffer);
}

// Build the WS-Discovery Probe SOAP envelope (NetworkVideoTransmitter type).
// Shared by the multicast (discover) and unicast (discover_unicast) paths.
static string _make_probe_message(const string& uuid)
{
    auto id = r_string_utils::format("urn:uuid:%s", uuid.c_str());
    return
    "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\"><SOAP-ENV:Header><a:Action SOAP-ENV:mustUnderstand=\"1\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action><a:MessageID>" + id + "</a:MessageID><a:ReplyTo><a:Address>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:Address></a:ReplyTo><a:To SOAP-ENV:mustUnderstand=\"1\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To></SOAP-ENV:Header><SOAP-ENV:Body><p:Probe xmlns:p=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\"><d:Types xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" xmlns:dp0=\"http://www.onvif.org/ver10/network/wsdl\">dp0:NetworkVideoTransmitter</d:Types></p:Probe></SOAP-ENV:Body></SOAP-ENV:Envelope>";
}

vector<r_onvif::discovery_reply> r_onvif::discover(const string& uuid, const std::function<bool()>& should_continue)
{
    string broadcast_message = _make_probe_message(uuid);

    vector<discovery_reply> all_discovered;

    // Get all available network adapters (UP, not loopback, multicast capable, with IPv4)
    auto adapters = r_utils::r_networking::r_get_adapters();

    // Send discovery probe on each interface and collect responses. Bail between
    // interfaces if the caller has asked us to stop (e.g. app shutdown), so we
    // don't pay the full per-interface listen window for every remaining adapter.
    for (const auto& adapter : adapters)
    {
        if (should_continue && !should_continue())
            break;
        auto responses = _discover_on_interface(adapter.ipv4_addr, broadcast_message, should_continue);
        all_discovered.insert(all_discovered.end(), responses.begin(), responses.end());
    }

    return all_discovered;
}

// Send the Probe directly (unicast UDP) to each target on port 3702 and collect
// any ProbeMatch responses. One socket: blast all probes, then read replies until
// the receive window goes quiet. Same response format as the multicast path.
static vector<r_onvif::discovery_reply> _probe_unicast(const vector<string>& targets, const string& broadcast_message, const std::function<bool()>& should_continue)
{
    vector<r_onvif::discovery_reply> discovered;
    if (targets.empty())
        return discovered;

#ifdef IS_WINDOWS
    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return discovered;

    BOOL reuse = TRUE;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    DWORD recvTimeout = 1000; // 1 second
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recvTimeout, sizeof(recvTimeout));
    _set_probe_send_timeout(sock);

    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(0);
    localAddr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr)) != 0) {
        closesocket(sock);
        return discovered;
    }

    for (const auto& tip : targets)
    {
        if (should_continue && !should_continue())
            break;
        struct sockaddr_in dst;
        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_port = htons(3702);
        if (inet_pton(AF_INET, tip.c_str(), &dst.sin_addr) != 1)
            continue;
        ::sendto(sock, broadcast_message.c_str(), (int)broadcast_message.length(), PROBE_SEND_FLAGS,
                 (struct sockaddr*)&dst, sizeof(dst));
    }

    char buf[8192];
    int timeoutCounts = 0;
    while (timeoutCounts < 2) {
        if (should_continue && !should_continue())
            break;
        struct sockaddr_in fromAddr;
        int fromAddrLen = sizeof(fromAddr);
        int len = ::recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&fromAddr, &fromAddrLen);
        if (len < 0) {
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT)
                timeoutCounts++;
            else
                break;
        }
        else if (len > 0) {
            buf[len] = '\0';
            discovered.push_back({_sender_ip(fromAddr), string(buf, len)});
        }
    }
    closesocket(sock);
#endif

#if defined(IS_LINUX) || defined(IS_MACOS)
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
        return discovered;

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct timeval recvTimeout;
    recvTimeout.tv_sec = 1;
    recvTimeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));
    _set_probe_send_timeout(sock);

    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(0);
    localAddr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
        close(sock);
        return discovered;
    }

    for (const auto& tip : targets)
    {
        if (should_continue && !should_continue())
            break;
        struct sockaddr_in dst;
        memset(&dst, 0, sizeof(dst));
        dst.sin_family = AF_INET;
        dst.sin_port = htons(3702);
        if (inet_pton(AF_INET, tip.c_str(), &dst.sin_addr) != 1)
            continue;
        sendto(sock, broadcast_message.c_str(), broadcast_message.length(), PROBE_SEND_FLAGS,
               (struct sockaddr*)&dst, sizeof(dst));
    }

    char buf[8192];
    int timeoutCounts = 0;
    while (timeoutCounts < 2) {
        if (should_continue && !should_continue())
            break;
        struct sockaddr_in fromAddr;
        socklen_t fromAddrLen = sizeof(fromAddr);
        int len = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&fromAddr, &fromAddrLen);
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                timeoutCounts++;
            else
                break;
        }
        else if (len > 0) {
            buf[len] = '\0';
            discovered.push_back({_sender_ip(fromAddr), string(buf, len)});
        }
    }
    close(sock);
#endif

    return discovered;
}

vector<r_onvif::discovery_reply> r_onvif::discover_unicast(const string& uuid, const vector<string>& target_ips, const std::function<bool()>& should_continue)
{
    if (target_ips.empty())
        return {};
    return _probe_unicast(target_ips, _make_probe_message(uuid), should_continue);
}

std::vector<discovered_info> r_onvif::filter_discovered(const std::vector<discovery_reply>& discovered)
{
    std::vector<discovered_info> filtered;
    std::map<std::string, bool> hosts_seen;
    for( const auto& reply : discovered)
    {
        try
        {
            pugi::xml_document doc;
            pugi::xml_parse_result result = doc.load_string(reply.envelope.c_str());
            if (!result)
                throw std::runtime_error("Failed to parse XML: " + std::string(result.description()));
            
            // Define namespaces
            const char* d_ns_uri = "http://schemas.xmlsoap.org/ws/2005/04/discovery";
            const char* s_ns_uri = "http://www.w3.org/2003/05/soap-envelope";
            const char* a_ns_uri = "http://schemas.xmlsoap.org/ws/2004/08/addressing";
            
            // Use XPath with namespace to find XAddrs nodes
            std::string xpath_query = "//*[local-name()='XAddrs' and namespace-uri()='" + std::string(d_ns_uri) + "']";
            pugi::xpath_query query(xpath_query.c_str());
            pugi::xpath_node_set nodes = query.evaluate_node_set(doc);
            
            // Use XPath to find Address node
            std::string addr_xpath = "//*[local-name()='Body' and namespace-uri()='" + std::string(s_ns_uri) + "']"
                                    "/*[local-name()='ProbeMatches' and namespace-uri()='" + std::string(d_ns_uri) + "']"
                                    "/*[local-name()='ProbeMatch' and namespace-uri()='" + std::string(d_ns_uri) + "']"
                                    "/*[local-name()='EndpointReference' and namespace-uri()='" + std::string(a_ns_uri) + "']"
                                    "/*[local-name()='Address' and namespace-uri()='" + std::string(a_ns_uri) + "']";
            
            pugi::xpath_query addr_query(addr_xpath.c_str());
            pugi::xpath_node addr_node = addr_query.evaluate_node(doc);
            
            // Get the address value
            std::string address = "";
            if (addr_node)
            {
                address = addr_node.node().text().get();
            }
            
            // Use XPath to find Scopes node
            std::string scopes_xpath = "//*[local-name()='Body' and namespace-uri()='" + std::string(s_ns_uri) + "']"
                                      "/*[local-name()='ProbeMatches' and namespace-uri()='" + std::string(d_ns_uri) + "']"
                                      "/*[local-name()='ProbeMatch' and namespace-uri()='" + std::string(d_ns_uri) + "']"
                                      "/*[local-name()='Scopes' and namespace-uri()='" + std::string(d_ns_uri) + "']";
            
            pugi::xpath_query scopes_query(scopes_xpath.c_str());
            pugi::xpath_node scopes_node = scopes_query.evaluate_node(doc);
            
            // Get the scopes value
            std::string scopes = "";
            if (scopes_node)
            {
                scopes = scopes_node.node().text().get();
            }
            
            std::vector<std::string> xaddrs_services;
            for (pugi::xpath_node node : nodes)
            {
                std::string text = node.node().text().get();
                std::vector<std::string> addresses = r_string_utils::split(text, " ");
                xaddrs_services.insert(end(xaddrs_services), begin(addresses), end(addresses));
            }
            if (xaddrs_services.empty())
                throw std::runtime_error("No ONVIF services found1.");

            // Only services on the host that answered. A reply naming another
            // host's address is dropped whole.
            {
                std::vector<std::string> own;
                for (auto& service : xaddrs_services)
                {
                    string host, protocol, uri;
                    int port = 0;
                    try { r_http::parse_url_parts(service, host, port, protocol, uri); }
                    catch (const std::exception&) { continue; }
                    if (!reply.sender.empty() && host == reply.sender)
                        own.push_back(service);
                }
                if (own.empty())
                {
                    R_LOG_WARNING("ONVIF discovery: ignored a reply from %s whose service addresses are on another host",
                                  reply.sender.empty() ? "an unknown address" : reply.sender.c_str());
                    continue;
                }
                xaddrs_services = own;
            }

            // Find first http/https service, preferring non-link-local addresses
            int first_valid_index = -1;
            int first_link_local_index = -1;  // Fallback if only link-local is available
            for(size_t i = 0; i < xaddrs_services.size(); ++i)
            {
                auto service = xaddrs_services[i];
                string url, host, protocol, uri;
                int port;
                r_http::parse_url_parts(service, host, port, protocol, uri);
                if(protocol == "http" || protocol == "https")
                {
                    if(_is_link_local_address(host))
                    {
                        // Remember link-local as fallback, but keep looking for better
                        if(first_link_local_index == -1)
                            first_link_local_index = (int)i;
                    }
                    else
                    {
                        // Found a non-link-local address - use it
                        first_valid_index = (int)i;
                        break;
                    }
                }
            }

            // If no non-link-local address found, fall back to link-local
            if(first_valid_index == -1)
                first_valid_index = first_link_local_index;

#if 0
            // Connection test disabled - accept all parseable XAddrs
            int first_connnected_index = -1;
            for(size_t i = 0; i < xaddrs_services.size(); ++i)
            {
                auto service = xaddrs_services[i];
                string url, host, protocol, uri;
                int port;
                r_http::parse_url_parts(service, host, port, protocol, uri);
                if(protocol != "http" && protocol != "https")
                    continue;

                try
                {
                    if(protocol == "http")
                    {
                        r_socket socket;
                        socket.set_io_timeout(5000);
                        socket.connect(host, port);
                        socket.close();
                        first_connnected_index = (int)i;
                        break;
                    }
                    else if(protocol == "https")
                    {
                        r_ssl_socket socket;
                        socket.set_io_timeout(5000);
                        socket.connect(host, port);
                        socket.close();
                        first_connnected_index = (int)i;
                        break;
                    }
                }
                catch(...)
                {
                    // ignoring individual connection errors...
                }
            }
            // But, we do need to throw if we couldn't connect to ANY of the services.
            if(first_connnected_index == -1)
                throw std::runtime_error("No ONVIF services found2.");
#endif

            if(first_valid_index == -1)
                throw std::runtime_error("No ONVIF services found2.");
            discovered_info di;
            r_http::parse_url_parts(xaddrs_services[first_valid_index], di.host, di.port, di.protocol, di.uri);
            di.address = address; // Assign the extracted address to the discovered_info struct

            auto mfgr = _get_scope_field(scopes, (char*)"onvif://www.onvif.org/name/");
            auto hdwr = _get_scope_field(scopes, (char*)"onvif://www.onvif.org/hardware/");

            // SAMSUNG E4500n
            // SAMSUNG 192.168.1.11
            // E4500n 192

            string camera_name;

            if(!mfgr.is_null())
                camera_name = mfgr.value();

            if(camera_name.empty())
                camera_name = di.host;

            di.camera_name = camera_name;

            if(hosts_seen.find(di.host) == hosts_seen.end())
            {
                filtered.push_back(di);
                hosts_seen[di.host] = true;
            }
        }
        catch(...)
        {
            // Some problem parsing something we discovered. squelching here so we can continue.
        }
    }
    return filtered;
}

r_onvif::r_onvif_cam::r_onvif_cam(const std::string& host, int port, const std::string& protocol, const std::string& uri, const r_utils::r_nullable<std::string>& username, const r_utils::r_nullable<std::string>& password, soap_version soap_hint, auth_mode auth_hint)
{
    _service_host = host;
    _service_port = port;
    _service_protocol = protocol;
    _service_uri = uri;
    _soap_ver = soap_hint;
    _auth_mode = auth_hint;

    _username = username;
    _password = password;

    auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    time_t camera_time = now;

    // ONVIF spec requires GetSystemDateAndTime to be accessible without authentication.
    // Try unauthenticated first so we can determine the clock offset before making
    // WS-Security digest requests (digest tokens are time-sensitive).
    bool time_synced = false;
    try
    {
        camera_time = get_camera_system_date_and_time(r_nullable<string>(), r_nullable<string>());
        time_synced = true;
    }
    catch (...) {}

    if (!time_synced)
    {
        const int max_time_sync_attempts = 3;
        for (int attempt = 0; attempt < max_time_sync_attempts; ++attempt)
        {
            try
            {
                camera_time = get_camera_system_date_and_time(_username, _password);
                break;
            }
            catch (const std::exception& ex)
            {
                if (attempt + 1 < max_time_sync_attempts)
                {
                    int delay_ms = 1000 * (1 << attempt); // 1s, 2s
                    R_LOG_INFO("GetSystemDateAndTime attempt %d/%d failed: %s, retrying in %dms...",
                               attempt + 1, max_time_sync_attempts, ex.what(), delay_ms);
                    this_thread::sleep_for(chrono::milliseconds(delay_ms));
                    now = chrono::system_clock::to_time_t(chrono::system_clock::now());
                }
                else
                    R_LOG_INFO("GetSystemDateAndTime failed after %d attempts: %s. Assuming zero clock offset.",
                               max_time_sync_attempts, ex.what());
            }
        }
    }

    _time_offset_seconds = (int)((int64_t)camera_time - (int64_t)now);
}

// Helper to check if response indicates SOAP version mismatch
static bool _is_soap_version_error(int status, const string& body)
{
    // HTTP 400 (Bad Request) or 415 (Unsupported Media Type) often indicate version mismatch
    if (status == 400 || status == 415)
        return true;

    // Check for SOAP Fault about envelope/namespace
    if (body.find("VersionMismatch") != string::npos ||
        body.find("MustUnderstand") != string::npos ||
        body.find("InvalidEnvelopeNamespace") != string::npos ||
        (body.find("Fault") != string::npos && body.find("envelope") != string::npos))
        return true;

    return false;
}

// Helper to build SOAP envelope with specified version and auth mode
static string _build_soap_envelope(
    soap_version ver,
    auth_mode auth,
    const function<void(pugi::xml_node&)>& build_body,
    const r_nullable<string>& username,
    const r_nullable<string>& password,
    int time_offset_seconds,
    const function<void(pugi::xml_node&)>& build_header = {}
)
{
    pugi::xml_document doc;

    // Add XML declaration
    pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";

    // Create envelope with version-specific namespace
    pugi::xml_node envelope = doc.append_child("SOAP-ENV:Envelope");

    if (ver == soap_version::soap_1_2)
        envelope.append_attribute("xmlns:SOAP-ENV") = "http://www.w3.org/2003/05/soap-envelope";
    else
        envelope.append_attribute("xmlns:SOAP-ENV") = "http://schemas.xmlsoap.org/soap/envelope/";

    // Common ONVIF namespaces
    envelope.append_attribute("xmlns:tds") = "http://www.onvif.org/ver10/device/wsdl";
    envelope.append_attribute("xmlns:trt") = "http://www.onvif.org/ver10/media/wsdl";
    envelope.append_attribute("xmlns:tt") = "http://www.onvif.org/ver10/schema";
    // Events: WS-Addressing, WS-BaseNotification and the ONVIF events service.
    envelope.append_attribute("xmlns:wsa") = "http://www.w3.org/2005/08/addressing";
    envelope.append_attribute("xmlns:wsnt") = "http://docs.oasis-open.org/wsn/b-2";
    envelope.append_attribute("xmlns:tev") = "http://www.onvif.org/ver10/events/wsdl";
    envelope.append_attribute("xmlns:tan") = "http://www.onvif.org/ver20/analytics/wsdl";

    // Add authentication header if credentials are provided
    if (username && password)
    {
        if (auth == auth_mode::text)
            _add_username_text_header(&doc, envelope, username.value(), password.value(), time_offset_seconds);
        else
            _add_username_digest_header(&doc, envelope, username.value(), password.value(), time_offset_seconds);
    }

    // Further headers (WS-Addressing, echoed ReferenceParameters) go in the same
    // Header the security token is in, or a new one when there are no credentials.
    if (build_header)
    {
        pugi::xml_node header = envelope.child("SOAP-ENV:Header");
        if (!header)
            header = envelope.prepend_child("SOAP-ENV:Header");
        build_header(header);
    }

    // Create body and let caller populate it
    pugi::xml_node body = envelope.append_child("SOAP-ENV:Body");
    build_body(body);

    // Serialize
    ostringstream oss;
    doc.save(oss, "", pugi::format_raw);
    return oss.str();
}

// Helper to check if response indicates authentication failure
static bool _is_auth_error(int status, const string& body)
{
    // HTTP 401 (Unauthorized) is a clear auth failure
    if (status == 401)
        return true;

    // Check for SOAP Fault about authentication
    if (body.find("NotAuthorized") != string::npos ||
        body.find("InvalidSecurity") != string::npos ||
        body.find("FailedAuthentication") != string::npos ||
        body.find("AuthenticationFailed") != string::npos ||
        body.find("InvalidSecurityToken") != string::npos ||
        body.find("FailedCheck") != string::npos)
        return true;

    return false;
}

// SOAP request with automatic SOAP 1.2->1.1 and Digest->Text fallback
pair<int, string> r_onvif::r_onvif_cam::_soap_request(
    const string& host,
    int port,
    const string& uri,
    const string& soap_action,
    const function<void(pugi::xml_node&)>& build_body,
    r_nullable<string> username,
    r_nullable<string> password,
    const function<void(pugi::xml_node&)>& build_header,
    bool log_request,
    int timeout_ms,
    int net_attempts
)
{
    // Determine which SOAP version(s) to try
    vector<soap_version> versions_to_try;
    if (_soap_ver == soap_version::unknown)
        versions_to_try = {soap_version::soap_1_2, soap_version::soap_1_1};
    else
        versions_to_try = {_soap_ver};

    // Determine which auth mode(s) to try
    vector<auth_mode> auth_modes_to_try;
    // PasswordDigest only, unless the caller asked for PasswordText outright:
    // a camera that rejects the digest never gets the password in plain text.
    if (_auth_mode == auth_mode::unknown)
        auth_modes_to_try = {auth_mode::digest};
    else
        auth_modes_to_try = {_auth_mode};

    pair<int, string> result;

    // Try each SOAP version
    for (auto ver : versions_to_try)
    {
        bool soap_ver_error_detected = false;

        // Try each auth mode for this SOAP version
        for (auto auth : auth_modes_to_try)
        {
            string body = _build_soap_envelope(ver, auth, build_body, username, password, _time_offset_seconds, build_header);

            // Pass SOAP version and action to set appropriate headers
            result = _http_interact(host, port, "POST", uri, body, ver, soap_action, timeout_ms, net_attempts, log_request);

            // Check if successful
            if (result.first == 200)
            {
                // Success - remember these settings for future requests
                if (_soap_ver == soap_version::unknown)
                    _soap_ver = ver;
                if (_auth_mode == auth_mode::unknown)
                    _auth_mode = auth;
                return result;
            }

            // Check if this is an auth error - try next auth mode
            if (_is_auth_error(result.first, result.second))
            {
                R_LOG_INFO("Auth mode %s rejected.",
                           auth == auth_mode::digest ? "Digest" : "Text");
                continue;
            }

            // Check if this is a SOAP version error - break inner loop, try next version
            if (_is_soap_version_error(result.first, result.second))
            {
                R_LOG_INFO("SOAP %s rejected, trying fallback...",
                           ver == soap_version::soap_1_2 ? "1.2" : "1.1");
                soap_ver_error_detected = true;
                break;
            }

            // Some other error - return immediately
            return result;
        }

        // Only try the next SOAP version if a genuine version error caused the break.
        // Cameras like Axis return HTTP 400 for auth failures too, so we cannot use
        // _is_soap_version_error() on the last result here -- it would trigger on the
        // auth-error response and send an unnecessary SOAP 1.1 request.
        if (soap_ver_error_detected)
            continue;

        break;
    }

    return result;
}

time_t r_onvif::r_onvif_cam::get_camera_system_date_and_time(
    r_nullable<string> username,
    r_nullable<string> password
)
{
    auto result = _soap_request(
        _service_host, _service_port, _service_uri,
        "http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime",
        [](pugi::xml_node& body) {
            body.append_child("tds:GetSystemDateAndTime");
        },
        username,
        password
    );

    auto maybe_parsed_ts = _parse_onvif_date_time(result.second);

    if(maybe_parsed_ts.is_null())
        throw std::runtime_error("Failed to parse ONVIF date and time.");

    return maybe_parsed_ts.value();
}

r_onvif::onvif_capabilities r_onvif::r_onvif_cam::get_camera_capabilities()
{
    auto result = _soap_request(
        _service_host, _service_port, _service_uri,
        "http://www.onvif.org/ver10/device/wsdl/GetCapabilities",
        [](pugi::xml_node& body) {
            pugi::xml_node capabilities = body.append_child("tds:GetCapabilities");
            pugi::xml_node category = capabilities.append_child("tds:Category");
            category.text().set("All");
        },
        _username,
        _password
    );

    if (result.first != 200)
        throw std::runtime_error("Failed to get camera capabilities: " + std::to_string(result.first));

    return result.second;
}

r_onvif::onvif_device_information r_onvif::r_onvif_cam::get_device_information()
{
    auto result = _soap_request(
        _service_host, _service_port, _service_uri,
        "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation",
        [](pugi::xml_node& body) {
            body.append_child("tds:GetDeviceInformation");
        },
        _username,
        _password
    );

    if (result.first != 200)
        throw std::runtime_error("Failed to get device information: " + std::to_string(result.first));

    pugi::xml_document doc;
    if (!doc.load_string(result.second.c_str()))
        throw std::runtime_error("Failed to parse GetDeviceInformationResponse XML");

    // Fields are in the device namespace; _select_node_local_abs matches on the
    // local name regardless of prefix, with a no-namespace fallback for cameras
    // that omit it.
    auto field = [&doc](const char* name) -> std::string {
        auto n = _select_node_local_abs(doc, name, NS_DEVICE);
        if (!n)
            n = _select_node_local_abs(doc, name);
        return n ? std::string(n.node().child_value()) : std::string();
    };

    onvif_device_information info;
    info.manufacturer     = field("Manufacturer");
    info.model            = field("Model");
    info.firmware_version = field("FirmwareVersion");
    info.serial_number    = field("SerialNumber");
    info.hardware_id      = field("HardwareId");
    return info;
}

r_onvif::onvif_media_service r_onvif::r_onvif_cam::get_media_service(const r_onvif::onvif_capabilities& capabilities) const
{
    pugi::xml_document doc;
    if (!doc.load_string(capabilities.c_str()))
        throw std::runtime_error("Failed to parse capabilities XML");

    // Use namespace-agnostic XPath: find Media element then XAddr within it
    // Try multiple namespace combinations since cameras vary

    pugi::xpath_node node;

    // Try 1: Media in schema namespace (most common in GetCapabilitiesResponse)
    string xpath = "//" + _xpath_local("Media", NS_SCHEMA) + "//" + _xpath_local("XAddr", NS_SCHEMA);
    node = doc.select_node(xpath.c_str());

    // Try 2: Media in device namespace
    if (!node)
    {
        xpath = "//" + _xpath_local("Media", NS_DEVICE) + "//" + _xpath_local("XAddr", NS_SCHEMA);
        node = doc.select_node(xpath.c_str());
    }

    // Try 3: Media in schema namespace, XAddr without namespace
    if (!node)
    {
        xpath = "//" + _xpath_local("Media", NS_SCHEMA) + "//" + _xpath_local("XAddr");
        node = doc.select_node(xpath.c_str());
    }

    // Try 4: Media in device namespace, XAddr without namespace
    if (!node)
    {
        xpath = "//" + _xpath_local("Media", NS_DEVICE) + "//" + _xpath_local("XAddr");
        node = doc.select_node(xpath.c_str());
    }

    // Try 5: No namespace constraints at all
    if (!node)
    {
        xpath = "//" + _xpath_local("Media") + "//" + _xpath_local("XAddr");
        node = doc.select_node(xpath.c_str());
    }

    if (!node)
        throw std::runtime_error("Media XAddr not found in capabilities");

    return node.node().child_value();
}

std::vector<r_onvif::onvif_profile_info> r_onvif::r_onvif_cam::get_profile_tokens(r_onvif::onvif_media_service media_service)
{
    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(media_service, host, port, protocol, uri);

    auto result = _soap_request(
        host, port, uri,
        "http://www.onvif.org/ver10/media/wsdl/GetProfiles",
        [](pugi::xml_node& body) {
            body.append_child("trt:GetProfiles");
        },
        _username,
        _password
    );

    if(result.first != 200)
        throw std::runtime_error("Failed to get camera profiles");

    try
    {
        return parse_profiles_response(result.second);
    }
    catch (const std::exception& exc)
    {
        printf("Error parsing profiles: %s\n", exc.what());
    }

    return {};
}

string r_onvif::r_onvif_cam::get_stream_uri(onvif_media_service media_service, onvif_profile_token profile_token)
{
    string host, http_protocol, uri;
    int port;
    r_http::parse_url_parts(media_service, host, port, http_protocol, uri);

    auto result = _soap_request(
        host, port, uri,
        "http://www.onvif.org/ver10/media/wsdl/GetStreamUri",
        [&profile_token](pugi::xml_node& body) {
            pugi::xml_node getStreamUri = body.append_child("trt:GetStreamUri");

            pugi::xml_node streamSetup = getStreamUri.append_child("trt:StreamSetup");

            pugi::xml_node stream = streamSetup.append_child("tt:Stream");
            stream.text().set("RTP-Unicast");

            pugi::xml_node transport = streamSetup.append_child("tt:Transport");

            pugi::xml_node protocol = transport.append_child("tt:Protocol");
            protocol.text().set("RTSP");

            pugi::xml_node profileTokenElem = getStreamUri.append_child("trt:ProfileToken");
            profileTokenElem.text().set(profile_token.c_str());
        },
        _username,
        _password
    );

    if(result.first != 200)
        throw std::runtime_error("Failed to get stream uri");

    // Use namespace-agnostic XPath
    pugi::xml_document doc;
    if (!doc.load_string(result.second.c_str()))
        throw std::runtime_error("Failed to parse GetStreamUri response");

    string xpath = "//" + _xpath_local("GetStreamUriResponse", NS_MEDIA) + "//" + _xpath_local("Uri", NS_SCHEMA);
    pugi::xpath_node node = doc.select_node(xpath.c_str());

    if (!node)
    {
        // Fallback: try without namespace constraint
        xpath = "//" + _xpath_local("GetStreamUriResponse") + "//" + _xpath_local("Uri");
        node = doc.select_node(xpath.c_str());
    }

    if (!node)
        throw std::runtime_error("Uri not found in GetStreamUri response");

    return node.node().child_value();
}

// ---------------------------------------------------------------------------
// Finding a camera's ONVIF device service.

// Whether an ONVIF device service answers at host:port/uri: an unauthenticated
// GetSystemDateAndTime gets back a SOAP envelope, the time or a fault (a camera
// that wants credentials or SOAP 1.1 still answers in SOAP). A web server that
// isn't ONVIF, a closed port or a hung service doesn't. A short timeout and a
// single attempt, so probing a wedged camera takes seconds, not minutes.
static bool _answers_onvif(const string& host, int port, const string& uri)
{
    try
    {
        auto body = _build_soap_envelope(
            soap_version::soap_1_2, auth_mode::digest,
            [](pugi::xml_node& b) { b.append_child("tds:GetSystemDateAndTime"); },
            r_nullable<string>(), r_nullable<string>(), 0
        );
        auto result = _http_interact(host, port, "POST", uri, body, soap_version::soap_1_2,
                                     "http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime", 4000, 1);
        return result.second.find("Envelope") != string::npos;
    }
    catch (...)
    {
        return false;
    }
}

r_nullable<r_onvif::onvif_endpoint> r_onvif::find_device_service(
    const string& host,
    const string& uri,
    r_nullable<int> hint_port,
    r_nullable<string> hint_protocol,
    const std::function<bool()>& should_continue
)
{
    auto path = uri.empty() ? string("/onvif/device_service") : uri;
    auto stopping = [&]() { return should_continue && !should_continue(); };

    if (!hint_port.is_null() && _answers_onvif(host, hint_port.value(), path))
        return onvif_endpoint{host, hint_port.value(), hint_protocol.is_null() ? string("http") : hint_protocol.value(), path};

    // The camera says where its service is, when it answers a unicast Probe.
    if (stopping())
        return r_nullable<onvif_endpoint>();
    for (auto& di : filter_discovered(discover_unicast(r_uuid::generate(), {host}, should_continue)))
    {
        if (di.host != host)
            continue;
        auto found_uri = di.uri.empty() ? path : di.uri;
        if (_answers_onvif(host, di.port, found_uri))
            return onvif_endpoint{host, di.port, di.protocol.empty() ? string("http") : di.protocol, found_uri};
    }

    const pair<int, const char*> common[] = {{80, "http"}, {8000, "http"}, {8080, "http"}, {443, "https"}};
    for (auto& c : common)
    {
        if (stopping())
            return r_nullable<onvif_endpoint>();
        if (!hint_port.is_null() && c.first == hint_port.value())
            continue;
        if (_answers_onvif(host, c.first, path))
        {
            R_LOG_INFO("onvif: device service for %s found on port %d", host.c_str(), c.first);
            return onvif_endpoint{host, c.first, c.second, path};
        }
    }

    R_LOG_WARNING("onvif: no device service found for %s (tried %s, unicast discovery, 80, 8000, 8080, 443)",
                  host.c_str(), hint_port.is_null() ? "no stored port" : ("stored port " + to_string(hint_port.value())).c_str());
    return r_nullable<onvif_endpoint>();
}

// ---------------------------------------------------------------------------
// Events: pull point subscriptions (ONVIF Core, Event service).

// The camera's own explanation from a SOAP fault, so a failed events call says
// why (e.g. "ter:InvalidArgVal", "Action not supported").
static string _fault_reason(const string& body)
{
    pugi::xml_document doc;
    if (!doc.load_string(body.c_str()))
        return body.substr(0, 300);
    string out;
    for (auto name : {"Value", "Text", "faultcode", "faultstring"})
    {
        string xp = "//" + _xpath_local(name);
        for (auto& n : doc.select_nodes(xp.c_str()))
        {
            string v = n.node().child_value();
            if (!v.empty())
                out += (out.empty() ? "" : " | ") + v;
        }
    }
    return out.empty() ? body.substr(0, 300) : out;
}

static runtime_error _events_error(const string& what, const pair<int, string>& result)
{
    return runtime_error(what + ": HTTP " + to_string(result.first) + " " + _fault_reason(result.second));
}

// The namespace a prefix means at node, from the nearest xmlns declaration.
static string _resolve_prefix(pugi::xml_node node, const string& prefix)
{
    string attr = prefix.empty() ? string("xmlns") : "xmlns:" + prefix;
    for (auto n = node; n; n = n.parent())
    {
        auto a = n.attribute(attr.c_str());
        if (a)
            return a.value();
    }
    return string();
}

// An element copied out of its document loses the xmlns declarations it
// inherited. Serialize it with every prefix it (or a descendant) uses declared.
static string _serialize_standalone(pugi::xml_node element)
{
    map<string, string> decls;
    auto note = [&](pugi::xml_node src, const string& qname) {
        auto colon = qname.find(':');
        string prefix = colon == string::npos ? string() : qname.substr(0, colon);
        if (prefix == "xmlns" || prefix == "xml" || decls.count(prefix))
            return;
        auto ns = _resolve_prefix(src, prefix);
        if (!ns.empty())
            decls[prefix] = ns;
    };
    function<void(pugi::xml_node)> visit = [&](pugi::xml_node n) {
        if (n.type() != pugi::node_element)
            return;
        note(n, n.name());
        for (auto a : n.attributes())
            if (string(a.name()).find(':') != string::npos)
                note(n, a.name());
        for (auto c : n.children())
            visit(c);
    };
    visit(element);

    pugi::xml_document doc;
    auto copy = doc.append_copy(element);
    for (auto& d : decls)
    {
        string attr = d.first.empty() ? string("xmlns") : "xmlns:" + d.first;
        if (!copy.attribute(attr.c_str()))
            copy.append_attribute(attr.c_str()) = d.second.c_str();
    }

    ostringstream oss;
    doc.save(oss, "", pugi::format_raw | pugi::format_no_declaration);
    return oss.str();
}

r_nullable<string> r_onvif::r_onvif_cam::get_events_service(const onvif_capabilities& capabilities) const
{
    pugi::xml_document doc;
    if (!doc.load_string(capabilities.c_str()))
        throw std::runtime_error("Failed to parse capabilities XML");
    string xp = "//" + _xpath_local("Events") + "/" + _xpath_local("XAddr");
    auto node = doc.select_node(xp.c_str());
    if (!node || string(node.node().child_value()).empty())
        return r_nullable<string>();
    return string(node.node().child_value());
}

// WS-Addressing Action and To: required by many cameras on events requests.
static function<void(pugi::xml_node&)> _addressing_headers(const string& action, const string& to)
{
    return [action, to](pugi::xml_node& header) {
        header.append_child("wsa:Action").text().set(action.c_str());
        header.append_child("wsa:MessageID").text().set(("urn:uuid:" + r_uuid::generate()).c_str());
        header.append_child("wsa:To").text().set(to.c_str());
    };
}

string r_onvif::r_onvif_cam::get_event_properties(const string& events_service)
{
    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(events_service, host, port, protocol, uri);

    const string action = "http://www.onvif.org/ver10/events/wsdl/EventPortType/GetEventPropertiesRequest";
    auto result = _soap_request(
        host, port, uri, action,
        [](pugi::xml_node& body) { body.append_child("tev:GetEventProperties"); },
        _username, _password,
        _addressing_headers(action, events_service)
    );
    if (result.first != 200)
        throw _events_error("GetEventProperties failed", result);
    return result.second;
}

r_nullable<string> r_onvif::r_onvif_cam::get_analytics_service(const onvif_capabilities& capabilities) const
{
    pugi::xml_document doc;
    if (!doc.load_string(capabilities.c_str()))
        throw std::runtime_error("Failed to parse capabilities XML");
    string xp = "//" + _xpath_local("Analytics") + "/" + _xpath_local("XAddr");
    auto node = doc.select_node(xp.c_str());
    if (!node || string(node.node().child_value()).empty())
        return r_nullable<string>();
    return string(node.node().child_value());
}

string r_onvif::r_onvif_cam::get_video_analytics_configurations(const onvif_media_service& media_service)
{
    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(media_service, host, port, protocol, uri);
    auto result = _soap_request(
        host, port, uri, "http://www.onvif.org/ver10/media/wsdl/GetVideoAnalyticsConfigurations",
        [](pugi::xml_node& body) { body.append_child("trt:GetVideoAnalyticsConfigurations"); },
        _username, _password
    );
    if (result.first != 200)
        throw _events_error("GetVideoAnalyticsConfigurations failed", result);
    return result.second;
}

// An analytics service request whose body is <tan:op><tan:ConfigurationToken>token</...></...>.
static function<void(pugi::xml_node&)> _analytics_body(const string& op, const string& configuration_token)
{
    return [op, configuration_token](pugi::xml_node& body) {
        auto req = body.append_child(("tan:" + op).c_str());
        req.append_child("tan:ConfigurationToken").text().set(configuration_token.c_str());
    };
}

string r_onvif::r_onvif_cam::get_supported_rules(const string& analytics_service, const string& configuration_token)
{
    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(analytics_service, host, port, protocol, uri);
    auto result = _soap_request(host, port, uri, "http://www.onvif.org/ver20/analytics/wsdl/GetSupportedRules",
                                _analytics_body("GetSupportedRules", configuration_token), _username, _password);
    if (result.first != 200)
        throw _events_error("GetSupportedRules failed", result);
    return result.second;
}

string r_onvif::r_onvif_cam::get_rules(const string& analytics_service, const string& configuration_token)
{
    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(analytics_service, host, port, protocol, uri);
    auto result = _soap_request(host, port, uri, "http://www.onvif.org/ver20/analytics/wsdl/GetRules",
                                _analytics_body("GetRules", configuration_token), _username, _password);
    if (result.first != 200)
        throw _events_error("GetRules failed", result);
    return result.second;
}

string r_onvif::r_onvif_cam::get_analytics_modules(const string& analytics_service, const string& configuration_token)
{
    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(analytics_service, host, port, protocol, uri);
    auto result = _soap_request(host, port, uri, "http://www.onvif.org/ver20/analytics/wsdl/GetAnalyticsModules",
                                _analytics_body("GetAnalyticsModules", configuration_token), _username, _password);
    if (result.first != 200)
        throw _events_error("GetAnalyticsModules failed", result);
    return result.second;
}

vector<uint8_t> r_onvif::packbits_decode(const vector<uint8_t>& in)
{
    vector<uint8_t> out;
    size_t i = 0;
    while (i < in.size())
    {
        int8_t n = (int8_t)in[i++];
        if (n >= 0)
        {
            for (int k = 0; k <= n && i < in.size(); ++k)
                out.push_back(in[i++]);
        }
        else if (n != -128)
        {
            if (i >= in.size())
                break;
            uint8_t b = in[i++];
            for (int k = 0; k < 1 - n; ++k)
                out.push_back(b);
        }
    }
    return out;
}

vector<uint8_t> r_onvif::packbits_encode(const vector<uint8_t>& in)
{
    vector<uint8_t> out;
    size_t i = 0, n = in.size();
    while (i < n)
    {
        size_t run = 1;
        while (i + run < n && run < 128 && in[i + run] == in[i])
            ++run;
        if (run >= 2)
        {
            out.push_back((uint8_t)(int8_t)(1 - (int)run));
            out.push_back(in[i]);
            i += run;
            continue;
        }
        // Literal bytes, up to the next run of two or more.
        size_t j = i;
        while (j < n && j - i < 128 && !(j + 1 < n && in[j] == in[j + 1]))
            ++j;
        if (j == i)
            j = i + 1;
        out.push_back((uint8_t)(j - i - 1));
        out.insert(out.end(), in.begin() + i, in.begin() + j);
        i = j;
    }
    return out;
}

bool r_onvif::cell_mask_empty(const string& active_cells, int cells)
{
    vector<uint8_t> bits;
    try { bits = packbits_decode(r_string_utils::from_base64(active_cells)); }
    catch (...) { return true; }
    for (int c = 0; c < cells && (size_t)(c / 8) < bits.size(); ++c)
        if (bits[c / 8] & (0x80 >> (c % 8)))
            return false;
    return true;
}

string r_onvif::full_cell_mask(int columns, int rows)
{
    int cells = columns * rows;
    vector<uint8_t> bits((cells + 7) / 8, 0xFF);
    if (cells % 8)
        bits.back() = (uint8_t)(0xFF << (8 - cells % 8));
    auto packed = packbits_encode(bits);
    return r_string_utils::to_base64(packed.data(), packed.size());
}

static bool _ends_with(const string& s, const string& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static string _local(const char* name)
{
    string s(name);
    auto colon = s.find(':');
    return colon == string::npos ? s : s.substr(colon + 1);
}

vector<r_onvif::onvif_cell_motion_rule> r_onvif::parse_cell_motion_rules(const string& xml)
{
    vector<onvif_cell_motion_rule> out;
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str()))
        throw std::runtime_error("Failed to parse GetVideoAnalyticsConfigurations response");

    for (auto cx : doc.select_nodes("//*[local-name()='Configurations']"))
    {
        auto config = cx.node();
        string token = config.attribute("token").value();
        int columns = 0, rows = 0;
        auto layout = config.select_node(".//*[local-name()='CellLayout']");
        if (layout)
        {
            columns = layout.node().attribute("Columns").as_int();
            rows = layout.node().attribute("Rows").as_int();
        }
        for (auto rx : config.select_nodes(".//*[local-name()='Rule']"))
        {
            auto rule = rx.node();
            if (!_ends_with(rule.attribute("Type").value(), "CellMotionDetector"))
                continue;
            onvif_cell_motion_rule r;
            r.configuration_token = token;
            r.rule_name = rule.attribute("Name").value();
            r.columns = columns;
            r.rows = rows;
            for (auto ix : rule.select_nodes(".//*[local-name()='SimpleItem']"))
                if (string(ix.node().attribute("Name").value()) == "ActiveCells")
                    r.active_cells = ix.node().attribute("Value").value();
            out.push_back(r);
        }
    }
    return out;
}

// A number from an ONVIF element: 0 when it's missing or not a number. FrameRateLimit is an int in the schema, but
// some cameras write "25.000000".
static int _int_child(const pugi::xml_node& parent, const char* xpath)
{
    auto node = parent.select_node(xpath);
    if (!node)
        return 0;
    char* end = nullptr;
    const char* text = node.node().child_value();
    double v = strtod(text, &end);
    return (end == text || v < 0 || v > 1e9) ? 0 : (int)v;
}

// A ReferenceToken: not empty, at most ONVIF_TOKEN_MAX_CHARS characters (UTF-8 continuation bytes don't count),
// no control characters.
static bool _valid_token(const string& token)
{
    if (token.empty())
        return false;
    size_t chars = 0;
    for (unsigned char c : token)
    {
        if (c < 0x20 || c == 0x7f)
            return false;
        if ((c & 0xc0) != 0x80)
            ++chars;
    }
    return chars <= r_onvif::ONVIF_TOKEN_MAX_CHARS;
}

// Element text without the whitespace around it, at most ONVIF_ENUM_MAX_BYTES, not cut inside a UTF-8 character.
static string _bounded_enum(const char* value)
{
    string s = r_string_utils::strip(value);
    if (s.size() <= r_onvif::ONVIF_ENUM_MAX_BYTES)
        return s;
    size_t n = r_onvif::ONVIF_ENUM_MAX_BYTES;
    while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80)
        --n;
    return s.substr(0, n);
}

vector<r_onvif::onvif_profile_info> r_onvif::parse_profiles_response(const string& xml)
{
    vector<onvif_profile_info> out;
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str()))
        throw std::runtime_error("Failed to parse GetProfiles response");

    for (auto px : doc.select_nodes("//*[local-name()='Profiles']"))
    {
        auto profile = px.node();
        onvif_profile_info info;
        info.token = profile.attribute("token").value();
        if (!_valid_token(info.token))
            continue;
        info.encoding = "Unknown";

        auto vsc = profile.select_node("./*[local-name()='VideoSourceConfiguration']");
        if (vsc)
        {
            // Element text, so a pretty-printed response may wrap it in whitespace.
            info.video_source_token = r_string_utils::strip(vsc.node().select_node("./*[local-name()='SourceToken']").node().child_value());
            if (!_valid_token(info.video_source_token))
                info.video_source_token.clear();
        }

        auto vec = profile.select_node("./*[local-name()='VideoEncoderConfiguration']");
        if (vec)
        {
            auto enc = vec.node();
            auto encoding = enc.select_node("./*[local-name()='Encoding']");
            if (encoding)
                info.encoding = _bounded_enum(encoding.node().child_value());
            info.width = (uint16_t)std::min(_int_child(enc, "./*[local-name()='Resolution']/*[local-name()='Width']"), 65535);
            info.height = (uint16_t)std::min(_int_child(enc, "./*[local-name()='Resolution']/*[local-name()='Height']"), 65535);
            info.h264_profile = _bounded_enum(enc.select_node("./*[local-name()='H264']/*[local-name()='H264Profile']").node().child_value());
            info.gov_length = _int_child(enc, "./*[local-name()='H264']/*[local-name()='GovLength']");
            info.fps_limit = _int_child(enc, "./*[local-name()='RateControl']/*[local-name()='FrameRateLimit']");
            info.bitrate_limit = _int_child(enc, "./*[local-name()='RateControl']/*[local-name()='BitrateLimit']");
        }
        out.push_back(info);
    }
    return out;
}

static bool _is_h264_or_h265(const string& encoding)
{
    auto e = r_string_utils::to_upper(encoding);
    return e == "H264" || e == "H265";
}

vector<r_onvif::onvif_profile_info> r_onvif::substream_candidates(const vector<onvif_profile_info>& profiles,
                                                                   const onvif_profile_info& main)
{
    const int64_t main_pixels = (int64_t)main.width * main.height;
    vector<onvif_profile_info> out;
    for (const auto& p : profiles)
    {
        const int64_t pixels = (int64_t)p.width * p.height;
        if (p.token == main.token || p.video_source_token != main.video_source_token || !_is_h264_or_h265(p.encoding))
            continue;
        if (pixels * 4 > main_pixels || p.width < SUBSTREAM_MIN_WIDTH)
            continue;
        out.push_back(p);
    }

    // Closest to 640x480 (640 wide or more first between equals), then H.264 over H.265, then Baseline or Main over
    // High (or not given), then the higher frame rate, then the camera's own order.
    auto rank = [](const onvif_profile_info& p, size_t camera_order) {
        const int64_t pixels = (int64_t)p.width * p.height;
        return std::make_tuple(
            pixels > SUBSTREAM_TARGET_PIXELS ? pixels - SUBSTREAM_TARGET_PIXELS : SUBSTREAM_TARGET_PIXELS - pixels,
            p.width >= 640 ? 0 : 1,
            r_string_utils::to_upper(p.encoding) == "H264" ? 0 : 1,
            (p.h264_profile == "Baseline" || p.h264_profile == "Main") ? 0 : 1,
            -p.fps_limit,
            camera_order);
    };
    vector<pair<decltype(rank(out.front(), 0)), size_t>> ranked;
    for (size_t i = 0; i < out.size(); ++i)
        ranked.push_back({rank(out[i], i), i});
    std::sort(ranked.begin(), ranked.end());
    vector<onvif_profile_info> sorted;
    for (auto& r : ranked)
        sorted.push_back(out[r.second]);
    return sorted;
}

vector<string> r_onvif::r_onvif_cam::fill_empty_cell_motion_regions(const onvif_capabilities& capabilities)
{
    vector<string> changed;
    auto analytics = get_analytics_service(capabilities);
    if (analytics.is_null())
        return changed;
    auto media = get_media_service(capabilities);
    auto configs = get_video_analytics_configurations(media);

    pugi::xml_document doc;
    if (!doc.load_string(configs.c_str()))
        throw std::runtime_error("Failed to parse GetVideoAnalyticsConfigurations response");

    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(analytics.value(), host, port, protocol, uri);

    for (auto& r : parse_cell_motion_rules(configs))
    {
        int cells = r.columns * r.rows;
        if (cells <= 0 || !cell_mask_empty(r.active_cells, cells))
            continue;

        // The rule as the camera has it, to send back with only ActiveCells changed.
        string xp = "//*[local-name()='Configurations'][@token='" + r.configuration_token +
                    "']//*[local-name()='Rule'][@Name='" + r.rule_name + "']";
        auto rule = doc.select_node(xp.c_str()).node();
        if (!rule)
            continue;
        auto full = full_cell_mask(r.columns, r.rows);

        auto result = _soap_request(
            host, port, uri, "http://www.onvif.org/ver20/analytics/wsdl/ModifyRules",
            [&](pugi::xml_node& body) {
                auto req = body.append_child("tan:ModifyRules");
                req.append_child("tan:ConfigurationToken").text().set(r.configuration_token.c_str());
                auto out = req.append_child("tan:Rule");
                out.append_attribute("Name") = r.rule_name.c_str();
                out.append_attribute("Type") = ("tt:" + _local(rule.attribute("Type").value())).c_str();
                auto params = out.append_child("tt:Parameters");
                auto in_params = rule.select_node("./*[local-name()='Parameters']").node();
                for (auto item : in_params.children())
                {
                    string local = _local(item.name());
                    if (local == "SimpleItem")
                    {
                        string name = item.attribute("Name").value();
                        auto si = params.append_child("tt:SimpleItem");
                        si.append_attribute("Name") = name.c_str();
                        si.append_attribute("Value") = (name == "ActiveCells") ? full.c_str() : item.attribute("Value").value();
                    }
                    else if (local == "ElementItem")
                        params.append_copy(item);
                }
            },
            _username, _password
        );
        if (result.first != 200)
            throw _events_error("ModifyRules failed for " + r.rule_name, result);

        // The camera may accept it and keep its own: check.
        bool kept = false;
        for (auto& after : parse_cell_motion_rules(get_video_analytics_configurations(media)))
            if (after.configuration_token == r.configuration_token && after.rule_name == r.rule_name)
                kept = !cell_mask_empty(after.active_cells, cells);
        if (!kept)
            throw std::runtime_error("the camera accepted a whole-frame region for " + r.rule_name + " but kept it empty");
        changed.push_back(r.configuration_token + "/" + r.rule_name);
    }
    return changed;
}

r_onvif::onvif_pull_point r_onvif::r_onvif_cam::create_pull_point_subscription(const string& events_service, int termination_seconds)
{
    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(events_service, host, port, protocol, uri);

    const string action = "http://www.onvif.org/ver10/events/wsdl/EventPortType/CreatePullPointSubscriptionRequest";
    auto result = _soap_request(
        host, port, uri, action,
        [termination_seconds](pugi::xml_node& body) {
            auto req = body.append_child("tev:CreatePullPointSubscription");
            req.append_child("tev:InitialTerminationTime").text().set(("PT" + to_string(termination_seconds) + "S").c_str());
        },
        _username, _password,
        _addressing_headers(action, events_service)
    );
    if (result.first != 200)
        throw _events_error("CreatePullPointSubscription failed", result);

    return parse_create_pull_point_response(result.second);
}

r_onvif::onvif_pull_point r_onvif::parse_create_pull_point_response(const string& xml)
{
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str()))
        throw runtime_error("CreatePullPointSubscription: unparseable response");

    onvif_pull_point pp;
    string xp = "//" + _xpath_local("SubscriptionReference") + "/" + _xpath_local("Address");
    auto addr = doc.select_node(xp.c_str());
    if (!addr)
        throw runtime_error("CreatePullPointSubscription: no SubscriptionReference/Address");
    pp.address = r_string_utils::strip(addr.node().child_value());

    xp = "//" + _xpath_local("SubscriptionReference") + "/" + _xpath_local("ReferenceParameters");
    auto refs = doc.select_node(xp.c_str());
    if (refs)
        for (auto child : refs.node().children())
            if (child.type() == pugi::node_element)
                pp.reference_parameters.push_back(_serialize_standalone(child));

    xp = "//" + _xpath_local("TerminationTime");
    auto term = doc.select_node(xp.c_str());
    if (term)
        pp.termination_time = term.node().child_value();

    return pp;
}

pair<int, string> r_onvif::r_onvif_cam::_pull_point_request(
    const onvif_pull_point& pull_point,
    const string& soap_action,
    const function<void(pugi::xml_node&)>& build_body,
    bool log_request,
    int timeout_ms,
    int net_attempts
)
{
    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(pull_point.address, host, port, protocol, uri);

    auto addressing = _addressing_headers(soap_action, pull_point.address);
    auto refs = pull_point.reference_parameters;
    return _soap_request(
        host, port, uri, soap_action, build_body, _username, _password,
        [addressing, refs](pugi::xml_node& header) {
            addressing(header);
            for (auto& r : refs)
            {
                pugi::xml_document tmp;
                if (!tmp.load_string(r.c_str()))
                    continue;
                auto copy = header.append_copy(tmp.document_element());
                copy.append_attribute("wsa:IsReferenceParameter") = "true";
            }
        },
        log_request,
        timeout_ms,
        net_attempts
    );
}

vector<r_onvif::onvif_event> r_onvif::r_onvif_cam::pull_messages(const onvif_pull_point& pull_point, int timeout_seconds, int message_limit)
{
    auto result = _pull_point_request(
        pull_point,
        "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesRequest",
        [timeout_seconds, message_limit](pugi::xml_node& body) {
            auto req = body.append_child("tev:PullMessages");
            req.append_child("tev:Timeout").text().set(("PT" + to_string(timeout_seconds) + "S").c_str());
            req.append_child("tev:MessageLimit").text().set(message_limit);
        },
        false,                              // every couple of seconds per camera: not worth a log line each
        (timeout_seconds + 5) * 1000,       // the camera holds it timeout_seconds; give up soon after
        1                                   // the subscriber retries by resubscribing
    );
    if (result.first != 200)
        throw _events_error("PullMessages failed", result);

    return parse_pull_messages_response(result.second);
}

vector<r_onvif::onvif_event> r_onvif::parse_pull_messages_response(const string& xml)
{
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str()))
        throw runtime_error("PullMessages: unparseable response");

    vector<onvif_event> events;
    string xp = "//" + _xpath_local("NotificationMessage");
    for (auto& nm : doc.select_nodes(xp.c_str()))
    {
        onvif_event ev;
        auto topic = nm.node().select_node(("./" + _xpath_local("Topic")).c_str());
        if (topic)
            ev.topic = r_string_utils::strip(topic.node().child_value());

        // wsnt:Message wraps the tt:Message that carries the data.
        auto msg = nm.node().select_node(("./" + _xpath_local("Message") + "/" + _xpath_local("Message")).c_str());
        if (msg)
        {
            auto m = msg.node();
            ev.utc_time = m.attribute("UtcTime").value();
            ev.property_operation = m.attribute("PropertyOperation").value();
            auto items = [&](const char* section, vector<pair<string, string>>& out) {
                string sx = "./" + _xpath_local(section) + "/" + _xpath_local("SimpleItem");
                for (auto& si : m.select_nodes(sx.c_str()))
                    out.emplace_back(si.node().attribute("Name").value(), si.node().attribute("Value").value());
            };
            items("Source", ev.source);
            items("Data", ev.data);
        }
        events.push_back(ev);
    }
    return events;
}

static string _local_name(const char* qname)
{
    string s = qname;
    auto c = s.find(':');
    return c == string::npos ? s : s.substr(c + 1);
}

static void _collect_topics(pugi::xml_node n, const string& path, vector<string>& out)
{
    for (auto c : n.children())
    {
        if (c.type() != pugi::node_element)
            continue;
        string name = _local_name(c.name());
        if (name == "MessageDescription" || name == "Documentation")
            continue;
        string p = path.empty() ? name : path + "/" + name;
        for (auto a : c.attributes())
            if (_local_name(a.name()) == "topic" && string(a.value()) == "true")
                out.push_back(p);
        _collect_topics(c, p, out);
    }
}

vector<string> r_onvif::parse_event_topics(const string& xml)
{
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str()))
        throw runtime_error("GetEventProperties: unparseable response");
    vector<string> topics;
    string xp = "//" + _xpath_local("TopicSet");
    auto ts = doc.select_node(xp.c_str());
    if (ts)
        _collect_topics(ts.node(), "", topics);
    return topics;
}

void r_onvif::r_onvif_cam::renew(const onvif_pull_point& pull_point, int termination_seconds)
{
    auto result = _pull_point_request(
        pull_point,
        "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/RenewRequest",
        [termination_seconds](pugi::xml_node& body) {
            body.append_child("wsnt:Renew").append_child("wsnt:TerminationTime").text().set(("PT" + to_string(termination_seconds) + "S").c_str());
        },
        true, 10000, 1
    );
    if (result.first != 200)
        throw _events_error("Renew failed", result);
}

void r_onvif::r_onvif_cam::unsubscribe(const onvif_pull_point& pull_point)
{
    auto result = _pull_point_request(
        pull_point,
        "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeRequest",
        [](pugi::xml_node& body) { body.append_child("wsnt:Unsubscribe"); },
        true, 5000, 1   // on the way out: a camera that has stopped answering mustn't hold up the stop
    );
    if (result.first != 200)
        throw _events_error("Unsubscribe failed", result);
}
