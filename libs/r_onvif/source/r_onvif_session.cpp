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

static pair<int, string> _http_interact(
    string host,
    int port,
    string http_method,
    string uri,
    string body
)
{
retry:
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

    request.write_request(*sock);

    r_http::r_client_response response;
    response.read_response(*sock);

    //sock->close();

    if(response.get_status() == 302)
    {
        string location = response.get_header("Location");
        R_LOG_INFO("Redirect response to %s", location.c_str());
        if(location.empty())
            throw std::runtime_error("Redirect response but no Location header");

        string protocol, new_uri;
        r_http::parse_url_parts(location, host, port, protocol, new_uri);

        // If the new URI is not the same as the original URI, update the URI
        if(new_uri != "/")
            uri = new_uri;

        R_LOG_INFO("Redirecting to (%s)(%d)(%s)", host.c_str(), port, uri.c_str());

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

static r_nullable<time_t> _parse_onvif_date_time(const std::string& xmlResponse)
{
    r_nullable<time_t> response;

    pugi::xml_document doc;
    doc.load_string(xmlResponse.c_str());

    // Check for DaylightSavings
    bool daylightSavings = false;
    pugi::xpath_node dstNode = doc.select_node("//tt:DaylightSavings");
    if (dstNode)
    {
        std::string dstValue = dstNode.node().child_value();
        daylightSavings = (dstValue == "true" || dstValue == "1");
    }
    
    // Try to get timezone information
    std::string timezone;
    pugi::xpath_node tzNode = doc.select_node("//tt:TZ");
    if (tzNode)
    {
        timezone = tzNode.node().child_value();
    }
    
    // First try to get UTCDateTime
    struct tm timeinfo = {};
    bool useUtc = false;
    
    pugi::xpath_node utcNode = doc.select_node("//tt:UTCDateTime");
    if (utcNode)
    {
        pugi::xml_node utcElement = utcNode.node();
        
        // Get year, month, day
        pugi::xpath_node dateNode = utcElement.select_node(".//tt:Date");
        if (dateNode)
        {
            pugi::xml_node dateElement = dateNode.node();
            
            pugi::xpath_node yearNode = dateElement.select_node(".//tt:Year");
            pugi::xpath_node monthNode = dateElement.select_node(".//tt:Month");
            pugi::xpath_node dayNode = dateElement.select_node(".//tt:Day");
            
            if (yearNode && monthNode && dayNode)
            {
                timeinfo.tm_year = std::stoi(yearNode.node().child_value()) - 1900;
                timeinfo.tm_mon = std::stoi(monthNode.node().child_value()) - 1;
                timeinfo.tm_mday = std::stoi(dayNode.node().child_value());
            }
        }
        
        // Get hour, minute, second
        pugi::xpath_node timeNode = utcElement.select_node(".//tt:Time");
        if (timeNode)
        {
            pugi::xml_node timeElement = timeNode.node();
            
            pugi::xpath_node hourNode = timeElement.select_node(".//tt:Hour");
            pugi::xpath_node minuteNode = timeElement.select_node(".//tt:Minute");
            pugi::xpath_node secondNode = timeElement.select_node(".//tt:Second");
            
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
        pugi::xpath_node localNode = doc.select_node("//tt:LocalDateTime");
        if (localNode)
        {
            pugi::xml_node localElement = localNode.node();
            
            // Get year, month, day
            pugi::xpath_node dateNode = localElement.select_node(".//tt:Date");
            if (dateNode)
            {
                pugi::xml_node dateElement = dateNode.node();
                
                pugi::xpath_node yearNode = dateElement.select_node(".//tt:Year");
                pugi::xpath_node monthNode = dateElement.select_node(".//tt:Month");
                pugi::xpath_node dayNode = dateElement.select_node(".//tt:Day");
                
                if (yearNode && monthNode && dayNode)
                {
                    timeinfo.tm_year = std::stoi(yearNode.node().child_value()) - 1900;
                    timeinfo.tm_mon = std::stoi(monthNode.node().child_value()) - 1;
                    timeinfo.tm_mday = std::stoi(dayNode.node().child_value());
                }
            }
            
            // Get hour, minute, second
            pugi::xpath_node timeNode = localElement.select_node(".//tt:Time");
            if (timeNode)
            {
                pugi::xml_node timeElement = timeNode.node();
                
                pugi::xpath_node hourNode = timeElement.select_node(".//tt:Hour");
                pugi::xpath_node minuteNode = timeElement.select_node(".//tt:Minute");
                pugi::xpath_node secondNode = timeElement.select_node(".//tt:Second");
                
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

static string _find_element_value(const string& xml, const vector<string>& path, const map<string, string>& namespaces)
{
    // Parse the XML document
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xml.c_str());
    if (!result)
        throw std::runtime_error("XML parsing failed: " + string(result.description()));

    // Start at document element
    pugi::xml_node currentNode = doc.document_element();

    // Helper function to check if element matches a name with namespace
    auto matchesElement = [&namespaces](const pugi::xml_node& element, const string& pathPart)
    {
        // Check if path part contains namespace prefix
        size_t colonPos = pathPart.find(':');
        if (colonPos != string::npos)
        {
            string nsPrefix = pathPart.substr(0, colonPos);
            string localName = pathPart.substr(colonPos + 1);

            // Look up namespace URI
            auto nsIter = namespaces.find(nsPrefix);
            if (nsIter != namespaces.end())
            {
                // PugiXML: Check namespace and local name
                string elementName = element.name();
                string elementPrefix;
                size_t elemColonPos = elementName.find(':');
                if (elemColonPos != string::npos)
                    elementPrefix = elementName.substr(0, elemColonPos);

                // Get namespace from element
                string elementNsUri = element.attribute("xmlns:" + elementPrefix).value();
                if (elementNsUri.empty())
                {
                    // Look for namespace in parent hierarchy
                    pugi::xml_node parent = element.parent();
                    while (parent && elementNsUri.empty())
                    {
                        elementNsUri = parent.attribute("xmlns:" + elementPrefix).value();
                        parent = parent.parent();
                    }
                }

                // Check if namespace matches and element name matches the local name
                if (elementNsUri == nsIter->second)
                {
                    string elemLocalName = (elemColonPos != string::npos) ? 
                        elementName.substr(elemColonPos + 1) : elementName;
                    return elemLocalName == localName;
                }
                return false;
            }
            return false;
        }
        else
        {
            // Just check node name directly if no namespace
            string elementName = element.name();
            size_t elemColonPos = elementName.find(':');
            string elemLocalName = (elemColonPos != string::npos) ? 
                elementName.substr(elemColonPos + 1) : elementName;
            
            return elemLocalName == pathPart || elementName == pathPart;
        }
    };

    // Navigate through each level of the path
    for (size_t i = 0; i < path.size(); i++)
    {
        const string& pathPart = path[i];
        bool found = false;

        // Check if current element matches this path part
        if (matchesElement(currentNode, pathPart))
        {
            found = true;
            // If this is the last part of the path, we've found our target
            if (i == path.size() - 1)
            {
                // Return the text content
                return currentNode.child_value();
            }

            // Navigate to first child for next iteration
            pugi::xml_node child = currentNode.first_child();
            while (child)
            {
                if (child.type() == pugi::node_element)
                {
                    currentNode = child;
                    break;
                }
                child = child.next_sibling();
            }
            if (!child)
                return ""; // No child elements to continue path
            continue; // Skip to next path part
        }

        // Look for matching child element
        pugi::xml_node child = currentNode.first_child();
        while (child)
        {
            if (child.type() == pugi::node_element)
            {
                if (matchesElement(child, pathPart))
                {
                    currentNode = child;
                    found = true;
                    break;
                }
            }
            child = child.next_sibling();
        }

        if (!found)
        {
            // Try to find any descendant that matches
            // PugiXML doesn't have a direct equivalent to getElementsByTagName
            // so we'll use a recursive approach or XPath
            string xpathQuery = ".//" + pathPart;
            pugi::xpath_node descendant = currentNode.select_node(xpathQuery.c_str());
            if (descendant)
            {
                currentNode = descendant.node();
                found = true;
            }
        }

        if (!found)
            return ""; // Path part not found

        // If this is the last part of the path, we've found our target
        if (i == path.size() - 1)
        {
            // Return the text content
            return currentNode.child_value();
        }
    }

    return ""; // Should not reach here if path is non-empty
}

static string _extract_value(const string& xmlDocument, const string& path, const map<string, string>& namespaces)
{
    vector<string> elementPath;
    size_t start = 0;
    size_t pos = 0;
 
    // Split path by '//' to create element path
    while ((pos = path.find("//", start)) != string::npos)
    {
        if (pos > start)
            elementPath.push_back(path.substr(start, pos - start));
        start = pos + 2;
    }
 
    // Add last element
    if (start < path.length())
        elementPath.push_back(path.substr(start));
 
    return _find_element_value(xmlDocument, elementPath, namespaces);
}

static string _extract_onvif_value(const string& xmlDocument, const string& path)
{
    map<string, string> namespaces =
    {
        {"s", "http://www.w3.org/2003/05/soap-envelope"},
        {"tds", "http://www.onvif.org/ver10/device/wsdl"},
        {"tt", "http://www.onvif.org/ver10/schema"},
        {"trt", "http://www.onvif.org/ver10/media/wsdl"},
        {"timg", "http://www.onvif.org/ver20/imaging/wsdl"},
        {"tev", "http://www.onvif.org/ver10/events/wsdl"},
        {"tan", "http://www.onvif.org/ver20/analytics/wsdl"},
        {"tptz", "http://www.onvif.org/ver20/ptz/wsdl"}
    };
 
    return _extract_value(xmlDocument, path, namespaces);
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

#ifdef IS_WINDOWS
static IN_ADDR _find_active_network_interface_windows()
{
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        // Return default interface if socket creation fails
        IN_ADDR defaultAddr;
        defaultAddr.s_addr = INADDR_ANY;
        return defaultAddr;
    }
    
    // Connect to Google's DNS to determine active interface
    sockaddr_in googleDns = {};
    googleDns.sin_family = AF_INET;
    googleDns.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &googleDns.sin_addr);
    
    if (connect(sock, (sockaddr*)&googleDns, sizeof(googleDns)) == SOCKET_ERROR) {
        closesocket(sock);
        // Return default interface if connection fails
        IN_ADDR defaultAddr;
        defaultAddr.s_addr = INADDR_ANY;
        return defaultAddr;
    }
    
    // Get local address
    sockaddr_in localAddr;
    int localAddrLen = sizeof(localAddr);
    if (getsockname(sock, (sockaddr*)&localAddr, &localAddrLen) == SOCKET_ERROR) {
        closesocket(sock);
        // Return default interface if getsockname fails
        IN_ADDR defaultAddr;
        defaultAddr.s_addr = INADDR_ANY;
        return defaultAddr;
    }
    
    closesocket(sock);
    return localAddr.sin_addr;
}
#endif

// Helper function to find active network interface on Linux/macOS
#if defined(IS_LINUX) || defined(IS_MACOS)
static struct in_addr _find_active_network_interface_linux()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        // Return default interface if socket creation fails
        struct in_addr defaultAddr;
        defaultAddr.s_addr = INADDR_ANY;
        return defaultAddr;
    }
    
    // Connect to Google's DNS to determine active interface
    struct sockaddr_in googleDns;
    memset(&googleDns, 0, sizeof(googleDns));
    googleDns.sin_family = AF_INET;
    googleDns.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &googleDns.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&googleDns, sizeof(googleDns)) < 0) {
        close(sock);
        // Return default interface if connection fails
        struct in_addr defaultAddr;
        defaultAddr.s_addr = INADDR_ANY;
        return defaultAddr;
    }
    
    // Get local address
    struct sockaddr_in localAddr;
    socklen_t localAddrLen = sizeof(localAddr);
    if (getsockname(sock, (struct sockaddr*)&localAddr, &localAddrLen) < 0) {
        close(sock);
        // Return default interface if getsockname fails
        struct in_addr defaultAddr;
        defaultAddr.s_addr = INADDR_ANY;
        return defaultAddr;
    }
    
    close(sock);
    return localAddr.sin_addr;
}
#endif

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

vector<string> r_onvif::discover(const string& uuid)
{
    auto id = r_string_utils::format("urn:uuid:%s", uuid.c_str());
    vector<string> discovered;

    string broadcast_message =
    "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\"><SOAP-ENV:Header><a:Action SOAP-ENV:mustUnderstand=\"1\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action><a:MessageID>" + id + "</a:MessageID><a:ReplyTo><a:Address>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:Address></a:ReplyTo><a:To SOAP-ENV:mustUnderstand=\"1\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To></SOAP-ENV:Header><SOAP-ENV:Body><p:Probe xmlns:p=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\"><d:Types xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" xmlns:dp0=\"http://www.onvif.org/ver10/network/wsdl\">dp0:NetworkVideoTransmitter</d:Types></p:Probe></SOAP-ENV:Body></SOAP-ENV:Envelope>";

#ifdef IS_WINDOWS
    // Windows-specific implementation
    SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        return discovered;
    }
    
    // Enable address reuse
    BOOL reuse = TRUE;
    if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) != 0) {
        printf("Set reuse address failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return discovered;
    }
    
    // Set receive timeout
    DWORD recvTimeout = 5000; // 5 seconds
    if (::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recvTimeout, sizeof(recvTimeout)) != 0) {
        printf("Set receive timeout failed: %d\n", WSAGetLastError());
    }
    
    // Find active interface
    IN_ADDR routable_addr = _find_active_network_interface_windows();
    
    // Bind to ANY address on a dynamic port
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(0); // Dynamic port
    localAddr.sin_addr.s_addr = INADDR_ANY;
    
    if (::bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr)) != 0) {
        printf("Bind failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return discovered;
    }
    
    // Set multicast interface
    if (::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&routable_addr, sizeof(routable_addr)) != 0) {
        printf("Set multicast interface failed: %d\n", WSAGetLastError());
    }
    
    // Join multicast group
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    mreq.imr_interface = routable_addr;
    
    if (::setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq)) != 0) {
        printf("Failed to join multicast group: %d\n", WSAGetLastError());
    }
    
    // Set multicast TTL
    DWORD ttl = 1;
    if (::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl)) != 0) {
        printf("Set multicast TTL failed: %d\n", WSAGetLastError());
    }
    
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
        printf("Send failed: %d\n", WSAGetLastError());
        closesocket(sock);
        return discovered;
    }
    
    printf("Sent discovery message (%d bytes)\n", bytesSent);
    
    // Receive responses
    printf("Waiting for responses...\n");
    
    char buf[8192];
    int timeoutCounts = 0;
    while (timeoutCounts < 2) {
        struct sockaddr_in fromAddr;
        int fromAddrLen = sizeof(fromAddr);
        int len = ::recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&fromAddr, &fromAddrLen);
        
        if (len < 0) {
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT) {
                printf("Receive timed out\n");
                timeoutCounts++;
            } else {
                printf("Receive error: %d\n", error);
                break;
            }
        } 
        else if (len > 0) {
            buf[len] = '\0';
            string response(buf, len);
            discovered.push_back(response);
        }
    }
    
    closesocket(sock);
#endif

#if defined(IS_LINUX) || defined(IS_MACOS)
    // Linux/macOS-specific implementation
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("Socket creation failed");
        return discovered;
    }
    
    // Enable address reuse
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("Set reuse address failed");
        close(sock);
        return discovered;
    }
    
    // Set receive timeout
    struct timeval recvTimeout;
    recvTimeout.tv_sec = 5;  // 5 seconds
    recvTimeout.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout)) < 0) {
        perror("Set receive timeout failed");
    }
    
    // Find active interface
    struct in_addr routable_addr = _find_active_network_interface_linux();
    
    // Bind to ANY address on a dynamic port
    struct sockaddr_in localAddr;
    memset(&localAddr, 0, sizeof(localAddr));
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(0); // Dynamic port
    localAddr.sin_addr.s_addr = INADDR_ANY;
    
    if (::bind(sock, (struct sockaddr*)&localAddr, sizeof(localAddr)) < 0) {
        perror("Bind failed");
        close(sock);
        return discovered;
    }
    
    // Set multicast interface
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &routable_addr, sizeof(routable_addr)) < 0) {
        perror("Set multicast interface failed");
    }
    
    // Join multicast group
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    mreq.imr_interface = routable_addr;
    
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("Failed to join multicast group");
    }
    
    // Set multicast TTL
    int ttl = 1;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        perror("Set multicast TTL failed");
    }
    
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
        perror("Send failed");
        close(sock);
        return discovered;
    }
    
    printf("Sent discovery message (%d bytes)\n", bytesSent);
    
    // Receive responses
    printf("Waiting for responses...\n");
    
    char buf[8192];
    int timeoutCounts = 0;
    while (timeoutCounts < 2) {
        struct sockaddr_in fromAddr;
        socklen_t fromAddrLen = sizeof(fromAddr);
        int len = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&fromAddr, &fromAddrLen);
        
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Receive timed out\n");
                timeoutCounts++;
            } else {
                perror("Receive error");
                break;
            }
        } 
        else if (len > 0) {
            buf[len] = '\0';
            string response(buf, len);
            discovered.push_back(response);
        }
    }
    
    close(sock);
#endif

    return discovered;
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
            discovered_info di;
            r_http::parse_url_parts(xaddrs_services[first_connnected_index], di.host, di.port, di.protocol, di.uri);
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

    auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    auto camera_time = get_camera_system_date_and_time();

    _time_offset_seconds = (int)((int64_t)camera_time - (int64_t)now);

    _username = username;
    _password = password;
}

time_t r_onvif::r_onvif_cam::get_camera_system_date_and_time() const
{
    // Create the equivalent SOAP XML document using PugiXML
    pugi::xml_document doc;

    // Add XML declaration
    pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";

    // Create the envelope element with namespace
    pugi::xml_node envelope = doc.append_child("SOAP-ENV:Envelope");
    envelope.append_attribute("xmlns:SOAP-ENV") = "http://www.w3.org/2003/05/soap-envelope";
    envelope.append_attribute("xmlns:tds") = "http://www.onvif.org/ver10/device/wsdl";

    // Create the body element with namespace
    pugi::xml_node body = envelope.append_child("SOAP-ENV:Body");

    // Create the GetSystemDateAndTime element with namespace
    body.append_child("tds:GetSystemDateAndTime");

    // Convert to string
    std::stringstream ss;
    doc.save(ss, "", pugi::format_raw); // format_raw for minimal formatting

    auto result = _http_interact(_service_host, _service_port, "POST", _service_uri, ss.str());

    auto maybe_parsed_ts = _parse_onvif_date_time(result.second);

    if(maybe_parsed_ts.is_null())
        throw std::runtime_error("Failed to parse ONVIF date and time.");

    return maybe_parsed_ts.value();
}

r_onvif::onvif_capabilities r_onvif::r_onvif_cam::get_camera_capabilities() const
{
    // Create XML document
    pugi::xml_document doc;
    
    // Add XML declaration
    pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";
    
    // Create root SOAP envelope element with namespace
    pugi::xml_node root = doc.append_child("SOAP-ENV:Envelope");
    root.append_attribute("xmlns:SOAP-ENV") = "http://www.w3.org/2003/05/soap-envelope";
    root.append_attribute("xmlns:tds") = "http://www.onvif.org/ver10/device/wsdl";
    
    // Add authentication header if credentials are provided
    if (_username && _password)
        _add_username_digest_header(&doc, root, _username.value(), _password.value(), _time_offset_seconds);
    
    // Create SOAP body
    pugi::xml_node body = root.append_child("SOAP-ENV:Body");
    
    // Create GetCapabilities element
    pugi::xml_node capabilities = body.append_child("tds:GetCapabilities");
    
    // Create Category element
    pugi::xml_node category = capabilities.append_child("tds:Category");
    
    // Set category text to "All"
    category.text().set("All");
    
    // Write XML to string
    std::ostringstream oss;
    doc.save(oss, "  ", pugi::format_default | pugi::format_indent);
    
    // Send HTTP request
    auto result = _http_interact(_service_host, _service_port, "POST", _service_uri, oss.str());
    
    // Check status code
    if (result.first != 200)
        throw std::runtime_error("Failed to get camera capabilities: " + std::to_string(result.first));
    
    return result.second;
}

r_onvif::onvif_media_service r_onvif::r_onvif_cam::get_media_service(const r_onvif::onvif_capabilities& capabilities) const
{
    return _extract_onvif_value(capabilities, "//tt:Media//tt:XAddr");
#if 0
    auto media_service = _extract_onvif_value(capabilities, "//tt:Media//tt:XAddr");

    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(media_service, host, port, protocol, uri);

    return uri;
#endif
}

std::vector<r_onvif::onvif_profile_info> r_onvif::r_onvif_cam::get_profile_tokens(r_onvif::onvif_media_service media_service)
{
    // Create the SOAP request document
    pugi::xml_document doc;

    // Add XML declaration
    pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";

    // Create root element with namespace
    pugi::xml_node root = doc.append_child("SOAP-ENV:Envelope");
    root.append_attribute("xmlns:SOAP-ENV") = "http://www.w3.org/2003/05/soap-envelope";
    root.append_attribute("xmlns:trt") = "http://www.onvif.org/ver10/media/wsdl";
    root.append_attribute("xmlns:tt") = "http://www.onvif.org/ver10/schema"; // Add this line

    // Add authentication header if credentials are provided
    if(_username && _password)
        _add_username_digest_header(&doc, root, _username.value(), _password.value(), _time_offset_seconds);

    // Create SOAP body
    pugi::xml_node body = root.append_child("SOAP-ENV:Body");

    // Create GetProfiles element
    body.append_child("trt:GetProfiles");

    // Serialize to string with pretty printing
    std::ostringstream oss;
    doc.save(oss, "  ", pugi::format_default | pugi::format_indent);
    auto request = oss.str();

    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(media_service, host, port, protocol, uri);

    // Send HTTP request
    auto result = _http_interact(host, port, "POST", uri, request);
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

        // Try multiple namespace patterns
        pugi::xpath_node_set profileNodes = response_doc.select_nodes("//tt:Profiles");
        if (profileNodes.empty())
            profileNodes = response_doc.select_nodes("//trt:Profiles");
        if (profileNodes.empty())
            profileNodes = response_doc.select_nodes("//Profiles");

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
            
            // Get video encoder configuration
            pugi::xpath_node videoEncoderConfigNode = profileElement.select_node(".//tt:VideoEncoderConfiguration");
            if (videoEncoderConfigNode)
            {
                pugi::xml_node videoEncoderConfigElement = videoEncoderConfigNode.node();
                
                // Get encoding
                pugi::xpath_node encodingNode = videoEncoderConfigElement.select_node(".//tt:Encoding");
                if (encodingNode)
                    profile.encoding = encodingNode.node().child_value();
                
                // Get resolution
                pugi::xpath_node resolutionNode = videoEncoderConfigElement.select_node(".//tt:Resolution");
                if (resolutionNode)
                {
                    pugi::xml_node resolutionElement = resolutionNode.node();
                    
                    pugi::xpath_node widthNode = resolutionElement.select_node(".//tt:Width");
                    pugi::xpath_node heightNode = resolutionElement.select_node(".//tt:Height");
                    
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
    // Create a new XML document
    pugi::xml_document doc;
    
    // Add XML declaration
    pugi::xml_node declaration = doc.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";
    
    // Create root element with namespace
    pugi::xml_node root = doc.append_child("SOAP-ENV:Envelope");
    root.append_attribute("xmlns:SOAP-ENV") = "http://www.w3.org/2003/05/soap-envelope";
    root.append_attribute("xmlns:trt") = "http://www.onvif.org/ver10/media/wsdl";
    root.append_attribute("xmlns:tt") = "http://www.onvif.org/ver10/schema";
    
    // Add authentication header if credentials are provided
    if(_username && _password)
        _add_username_digest_header(&doc, root, _username.value(), _password.value(), _time_offset_seconds);
    
    // Create Body element
    pugi::xml_node body = root.append_child("SOAP-ENV:Body");
    
    // Create GetStreamUri element
    pugi::xml_node getStreamUri = body.append_child("trt:GetStreamUri");
    
    // Create StreamSetup element
    pugi::xml_node streamSetup = getStreamUri.append_child("trt:StreamSetup");
    
    // Create Stream element
    pugi::xml_node stream = streamSetup.append_child("tt:Stream");
    stream.text().set("RTP-Unicast");
    
    // Create Transport element
    pugi::xml_node transport = streamSetup.append_child("tt:Transport");
    
    // Create Protocol element
    pugi::xml_node protocol = transport.append_child("tt:Protocol");
    protocol.text().set("RTSP");
    
    // Create ProfileToken element
    pugi::xml_node profileTokenElem = getStreamUri.append_child("trt:ProfileToken");
    profileTokenElem.text().set(profile_token.c_str());
    
    // Convert the document to a string for transmission
    std::ostringstream oss;
    doc.save(oss, "  ", pugi::format_default | pugi::format_indent);
    auto request = oss.str();

    string host, http_protocol, uri;
    int port;
    r_http::parse_url_parts(media_service, host, port, http_protocol, uri);

    auto result = _http_interact(host, port, "POST", uri, request);
    
    if(result.first != 200)
        throw std::runtime_error("Failed to get stream uri");
    
    return _extract_onvif_value(result.second, "s:Body//trt:GetStreamUriResponse//tt:Uri");
}
