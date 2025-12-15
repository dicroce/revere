#ifdef IS_WINDOWS
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#endif

#include <thread>
#include <sstream>
#include <map>
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

static pair<int, string> _http_interact(
    string host,
    int port,
    string http_method,
    string uri,
    string body,
    soap_version soap_ver = soap_version::unknown,
    const string& soap_action = ""
)
{
    int redirect_count = 0;

retry:
    if (redirect_count >= MAX_REDIRECTS)
        throw std::runtime_error("Too many redirects (max " + std::to_string(MAX_REDIRECTS) + ")");

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

    // Set SOAP-specific headers
    if (soap_ver == soap_version::soap_1_2)
    {
        // SOAP 1.2: Content-Type with action parameter
        if (!soap_action.empty())
            request.add_header("Content-Type", "application/soap+xml; charset=utf-8; action=\"" + soap_action + "\"");
        else
            request.add_header("Content-Type", "application/soap+xml; charset=utf-8");
    }
    else if (soap_ver == soap_version::soap_1_1)
    {
        // SOAP 1.1: text/xml + SOAPAction header
        request.add_header("Content-Type", "text/xml; charset=utf-8");
        if (!soap_action.empty())
            request.add_header("SOAPAction", "\"" + soap_action + "\"");
    }

    // Connection: close - some cameras don't handle keep-alive well
    request.add_header("Connection", "close");

    // User-Agent - some stacks behave differently with empty UA
    request.add_header("User-Agent", "ONVIF-Client/1.0");

    request.write_request(*sock, 30000);

    r_http::r_client_response response;
    response.read_response(*sock, 30000);

    //sock->close();

    if(response.get_status() == 302 || response.get_status() == 301 || response.get_status() == 307)
    {
        string location = response.get_header("Location");
        R_LOG_INFO("Redirect response to %s", location.c_str());
        if(location.empty())
            throw std::runtime_error("Redirect response but no Location header");

        // Check if Location is absolute or relative
        if (location.find("://") != string::npos)
        {
            // Absolute URL - parse all parts
            string protocol, new_uri;
            r_http::parse_url_parts(location, host, port, protocol, new_uri);
            uri = new_uri;
        }
        else if (location[0] == '/')
        {
            // Absolute path - keep host/port, use new path
            uri = location;
        }
        else
        {
            // Relative path - resolve against current URI
            size_t last_slash = uri.rfind('/');
            if (last_slash != string::npos)
                uri = uri.substr(0, last_slash + 1) + location;
            else
                uri = "/" + location;
        }

        R_LOG_INFO("Redirecting to (%s)(%d)(%s)", host.c_str(), port, uri.c_str());

        redirect_count++;
        goto retry;
    }

    auto maybe_body = response.get_body_as_string();

    if(maybe_body.is_null())
        return make_pair(response.get_status(), string());

    return make_pair(response.get_status(), maybe_body.value());
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

// Helper function to discover ONVIF devices on a specific interface
static vector<string> _discover_on_interface(const string& interface_ip, const string& broadcast_message)
{
    vector<string> discovered;

#ifdef IS_WINDOWS
    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
        return discovered;

    // Enable address reuse
    BOOL reuse = TRUE;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    // Set receive timeout
    DWORD recvTimeout = 5000; // 5 seconds
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recvTimeout, sizeof(recvTimeout));

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

    // Send discovery message
    int bytesSent = ::sendto(sock, broadcast_message.c_str(), (int)broadcast_message.length(), 0,
                            (struct sockaddr*)&multicastAddr, sizeof(multicastAddr));

    if (bytesSent < 0) {
        closesocket(sock);
        return discovered;
    }

    // Receive responses
    char buf[8192];
    int timeoutCounts = 0;
    while (timeoutCounts < 2) {
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
            discovered.push_back(string(buf, len));
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

    // Set receive timeout
    struct timeval recvTimeout;
    recvTimeout.tv_sec = 5;
    recvTimeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));

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

    // Send discovery message
    int bytesSent = sendto(sock, broadcast_message.c_str(), broadcast_message.length(), 0,
                          (struct sockaddr*)&multicastAddr, sizeof(multicastAddr));

    if (bytesSent < 0) {
        close(sock);
        return discovered;
    }

    // Receive responses
    char buf[8192];
    int timeoutCounts = 0;
    while (timeoutCounts < 2) {
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
            discovered.push_back(string(buf, len));
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

vector<string> r_onvif::discover(const string& uuid)
{
    auto id = r_string_utils::format("urn:uuid:%s", uuid.c_str());

    string broadcast_message =
    "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\"><SOAP-ENV:Header><a:Action SOAP-ENV:mustUnderstand=\"1\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action><a:MessageID>" + id + "</a:MessageID><a:ReplyTo><a:Address>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:Address></a:ReplyTo><a:To SOAP-ENV:mustUnderstand=\"1\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To></SOAP-ENV:Header><SOAP-ENV:Body><p:Probe xmlns:p=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\"><d:Types xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" xmlns:dp0=\"http://www.onvif.org/ver10/network/wsdl\">dp0:NetworkVideoTransmitter</d:Types></p:Probe></SOAP-ENV:Body></SOAP-ENV:Envelope>";

    vector<string> all_discovered;

    // Get all available network adapters (UP, not loopback, multicast capable, with IPv4)
    auto adapters = r_utils::r_networking::r_get_adapters();

    // Send discovery probe on each interface and collect responses
    for (const auto& adapter : adapters)
    {
        auto responses = _discover_on_interface(adapter.ipv4_addr, broadcast_message);
        all_discovered.insert(all_discovered.end(), responses.begin(), responses.end());
    }

    return all_discovered;
}

std::vector<discovered_info> r_onvif::filter_discovered(const std::vector<std::string>& discovered)
{
    std::vector<discovered_info> filtered;
    std::map<std::string, bool> hosts_seen;
    for( auto d : discovered)
    {
        try
        {
            pugi::xml_document doc;
            pugi::xml_parse_result result = doc.load_string(d.c_str());
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

            // Find first http/https service
            int first_valid_index = -1;
            for(size_t i = 0; i < xaddrs_services.size(); ++i)
            {
                auto service = xaddrs_services[i];
                string url, host, protocol, uri;
                int port;
                r_http::parse_url_parts(service, host, port, protocol, uri);
                if(protocol == "http" || protocol == "https")
                {
                    first_valid_index = (int)i;
                    break;
                }
            }

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

r_onvif::r_onvif_cam::r_onvif_cam(const std::string& host, int port, const std::string& protocol, const std::string& uri, const r_utils::r_nullable<std::string>& username, const r_utils::r_nullable<std::string>& password)
{
    _service_host = host;
    _service_port = port;
    _service_protocol = protocol;
    _service_uri = uri;
    _soap_ver = soap_version::unknown;
    _auth_mode = auth_mode::unknown;

    auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    auto camera_time = get_camera_system_date_and_time();

    _time_offset_seconds = (int)((int64_t)camera_time - (int64_t)now);

    _username = username;
    _password = password;
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
    int time_offset_seconds
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

    // Add authentication header if credentials are provided
    if (username && password)
    {
        if (auth == auth_mode::text)
            _add_username_text_header(&doc, envelope, username.value(), password.value(), time_offset_seconds);
        else
            _add_username_digest_header(&doc, envelope, username.value(), password.value(), time_offset_seconds);
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
    const function<void(pugi::xml_node&)>& build_body
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
    if (_auth_mode == auth_mode::unknown)
        auth_modes_to_try = {auth_mode::digest, auth_mode::text};
    else
        auth_modes_to_try = {_auth_mode};

    pair<int, string> result;

    // Try each SOAP version
    for (auto ver : versions_to_try)
    {
        // Try each auth mode for this SOAP version
        for (auto auth : auth_modes_to_try)
        {
            string body = _build_soap_envelope(ver, auth, build_body, _username, _password, _time_offset_seconds);

            // Pass SOAP version and action to set appropriate headers
            result = _http_interact(host, port, "POST", uri, body, ver, soap_action);

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
                R_LOG_INFO("Auth mode %s rejected, trying fallback...",
                           auth == auth_mode::digest ? "Digest" : "Text");
                continue;
            }

            // Check if this is a SOAP version error - break inner loop, try next version
            if (_is_soap_version_error(result.first, result.second))
            {
                R_LOG_INFO("SOAP %s rejected, trying fallback...",
                           ver == soap_version::soap_1_2 ? "1.2" : "1.1");
                break;
            }

            // Some other error - return immediately
            return result;
        }

        // If we broke out of auth loop due to version error, continue to next version
        if (_is_soap_version_error(result.first, result.second))
            continue;

        // If auth loop exhausted without success, don't try other versions
        break;
    }

    return result;
}

time_t r_onvif::r_onvif_cam::get_camera_system_date_and_time()
{
    auto result = _soap_request(
        _service_host, _service_port, _service_uri,
        "http://www.onvif.org/ver10/device/wsdl/GetSystemDateAndTime",
        [](pugi::xml_node& body) {
            body.append_child("tds:GetSystemDateAndTime");
        }
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
        }
    );

    if (result.first != 200)
        throw std::runtime_error("Failed to get camera capabilities: " + std::to_string(result.first));

    return result.second;
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
        }
    );

    if(result.first != 200)
        throw std::runtime_error("Failed to get camera profiles");

    vector<onvif_profile_info> profiles;
    try
    {
        // Parse the response
        pugi::xml_document response_doc;
        pugi::xml_parse_result parse_result = response_doc.load_string(result.second.c_str());
        if (!parse_result)
            throw std::runtime_error("XML parsing failed: " + std::string(parse_result.description()));

        // Use namespace-agnostic XPath to find Profiles elements
        string profiles_xpath = "//" + _xpath_local("Profiles", NS_MEDIA);
        pugi::xpath_node_set profileNodes = response_doc.select_nodes(profiles_xpath.c_str());

        if (profileNodes.empty())
        {
            // Fallback without namespace constraint
            profiles_xpath = "//" + _xpath_local("Profiles");
            profileNodes = response_doc.select_nodes(profiles_xpath.c_str());
        }

        // Process each profile
        for (const auto& profileNode : profileNodes)
        {
            pugi::xml_node profileElement = profileNode.node();
            onvif_profile_info profile;

            // Get profile token
            profile.token = profileElement.attribute("token").value();

            // Default values in case we can't find the data
            profile.encoding = "Unknown";
            profile.width = 0;
            profile.height = 0;

            // Get video encoder configuration using namespace-agnostic XPath
            string vec_xpath = ".//" + _xpath_local("VideoEncoderConfiguration", NS_SCHEMA);
            pugi::xpath_node videoEncoderConfigNode = profileElement.select_node(vec_xpath.c_str());

            if (!videoEncoderConfigNode)
            {
                // Fallback without namespace
                vec_xpath = ".//" + _xpath_local("VideoEncoderConfiguration");
                videoEncoderConfigNode = profileElement.select_node(vec_xpath.c_str());
            }

            if (videoEncoderConfigNode)
            {
                pugi::xml_node videoEncoderConfigElement = videoEncoderConfigNode.node();

                // Get encoding
                string enc_xpath = ".//" + _xpath_local("Encoding", NS_SCHEMA);
                pugi::xpath_node encodingNode = videoEncoderConfigElement.select_node(enc_xpath.c_str());
                if (!encodingNode)
                {
                    enc_xpath = ".//" + _xpath_local("Encoding");
                    encodingNode = videoEncoderConfigElement.select_node(enc_xpath.c_str());
                }
                if (encodingNode)
                    profile.encoding = encodingNode.node().child_value();

                // Get resolution
                string res_xpath = ".//" + _xpath_local("Resolution", NS_SCHEMA);
                pugi::xpath_node resolutionNode = videoEncoderConfigElement.select_node(res_xpath.c_str());
                if (!resolutionNode)
                {
                    res_xpath = ".//" + _xpath_local("Resolution");
                    resolutionNode = videoEncoderConfigElement.select_node(res_xpath.c_str());
                }

                if (resolutionNode)
                {
                    pugi::xml_node resolutionElement = resolutionNode.node();

                    string w_xpath = ".//" + _xpath_local("Width", NS_SCHEMA);
                    string h_xpath = ".//" + _xpath_local("Height", NS_SCHEMA);
                    pugi::xpath_node widthNode = resolutionElement.select_node(w_xpath.c_str());
                    pugi::xpath_node heightNode = resolutionElement.select_node(h_xpath.c_str());

                    if (!widthNode)
                    {
                        w_xpath = ".//" + _xpath_local("Width");
                        widthNode = resolutionElement.select_node(w_xpath.c_str());
                    }
                    if (!heightNode)
                    {
                        h_xpath = ".//" + _xpath_local("Height");
                        heightNode = resolutionElement.select_node(h_xpath.c_str());
                    }

                    if (widthNode)
                        profile.width = static_cast<uint16_t>(std::stoi(widthNode.node().child_value()));
                    if (heightNode)
                        profile.height = static_cast<uint16_t>(std::stoi(heightNode.node().child_value()));
                }
            }

            // Add the profile to our list
            profiles.push_back(profile);
        }
    }
    catch (const std::exception& exc)
    {
        printf("Error parsing profiles: %s\n", exc.what());
    }

    return profiles;
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
        }
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
