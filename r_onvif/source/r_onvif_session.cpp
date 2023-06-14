#ifdef IS_WINDOWS
#define _WINSOCK_DEPRECATED_NO_WARNINGS 1
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/stat.h>
#include "libxml/xpathInternals.h"
#include "r_onvif/r_onvif_session.h"
#include "r_utils/r_socket.h"
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

using namespace r_onvif;
using namespace r_utils;
using namespace r_utils::r_std_utils;
using namespace std;

r_onvif_session::r_onvif_session() :
    _buf(),
    _len(),
    _uuid(r_string_utils::format("urn:uuid:%s", r_uuid::generate().c_str())),
    _discovery_msg_id(1)
{
    xmlInitParser();
}

r_onvif_session::~r_onvif_session()
{
    xmlCleanupParser();
}

vector<r_onvif_discovery_info> r_onvif_session::discover()
{
    _uuid = r_string_utils::format("urn:uuid:%s", r_uuid::generate().c_str());

    vector<r_onvif_discovery_info> discovered;

    string broadcast_message;
    if(_discovery_msg_id == 1)
        broadcast_message = _get_discovery_xml();
    else if(_discovery_msg_id == 2)
        broadcast_message = _get_discovery_xml2();

    r_udp_socket socket;
    _set_socket_options(socket.fd());

    r_socket_address addr(3702, "239.255.255.250");

    socket.sendto((uint8_t*)broadcast_message.c_str(), broadcast_message.length(), addr);

    int i = 0;
    unsigned char looping = 1;
    while(looping)
    {
        _len[i] = socket.recvfrom((uint8_t*)_buf[i], sizeof(_buf[i]), addr);

        if(_len[i] > 0)
        {
            _buf[i][_len[i]] = '\0';

            try
            {
                r_onvif_discovery_info info;
                info.index = i;
                info.xaddrs = _extract_xaddrs(i);

                string host, protocol, uri;
                int port;
                r_http::parse_url_parts(info.xaddrs, host, port, protocol, uri);
                info.ipv4 = host;

                info.camera_name = _get_camera_name(i, info.ipv4);

                info.address = _extract_address(i);
                discovered.push_back(info);
            }
            catch(const std::exception& e)
            {
                R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
            }

            i++;

        } else looping = 0;
    }

    return discovered;
}

r_nullable<r_onvif_device_info> r_onvif_session::get_rtsp_url(
    const std::string& camera_name,
    const std::string& ipv4,
    const std::string& xaddrs,
    const std::string& address,
    r_nullable<string> username,
    r_nullable<string> password
) const
{
    r_nullable<r_onvif_device_info> result;

    auto service = _extract_onvif_service(xaddrs, true);

    auto timeOffset = _get_time_offset(service, xaddrs);

    raii_ptr<xmlDoc> capsDoc(_send_get_capabilities(username, password, xaddrs, timeOffset), xmlFreeDoc);

    if(!capsDoc.get())
        R_THROW(("Unable to fetch device capabilities."));

    _check_for_xml_error_msg(capsDoc.get(), xaddrs);

    auto media_service = _extract_onvif_service(_get_xml_value(capsDoc.get(), "//s:Body//tds:GetCapabilitiesResponse//tds:Capabilities//tt:Media//tt:XAddr", xaddrs), true);

    auto profile_token = _get_first_profile_token(username, password, xaddrs, media_service, timeOffset);

    auto rtsp_url = _get_stream_uri(username, password, timeOffset, profile_token, xaddrs, media_service);

    raii_ptr<xmlDoc> di_doc(_send_get_device_information(username, password, timeOffset, xaddrs, media_service), xmlFreeDoc);

    if(!di_doc.get())
        R_THROW(("Unable to fetch device information."));

    _check_for_xml_error_msg(di_doc.get(), xaddrs);

    auto serial_number = _get_xml_value(di_doc.get(), "//s:Body//tds:GetDeviceInformationResponse//tds:SerialNumber", xaddrs);
    auto model_number = _get_xml_value(di_doc.get(), "//s:Body//tds:GetDeviceInformationResponse//tds:Model", xaddrs);
    auto firmware_version = _get_xml_value(di_doc.get(), "//s:Body//tds:GetDeviceInformationResponse//tds:FirmwareVersion", xaddrs);
    auto manufacturer = _get_xml_value(di_doc.get(), "//s:Body//tds:GetDeviceInformationResponse//tds:Manufacturer", xaddrs);
    auto hardware_id = _get_xml_value(di_doc.get(), "//s:Body//tds:GetDeviceInformationResponse//tds:HardwareId", xaddrs);

    r_onvif_device_info rdi;
    rdi.camera_name = camera_name;
    rdi.ipv4 = ipv4;
    rdi.xaddrs = xaddrs;
    rdi.address = address;
    rdi.serial_number = serial_number;
    rdi.model_number = model_number;
    rdi.firmware_version = firmware_version;
    rdi.manufacturer = manufacturer;
    rdi.hardware_id = hardware_id;
    rdi.rtsp_url = rtsp_url;

    result.set_value(rdi);

    return result;
}

string r_onvif_session::_get_discovery_xml() const
{
    raii_ptr<xmlDoc> doc(xmlNewDoc((xmlChar*)"1.0"), xmlFreeDoc);
    xmlNodePtr root = xmlNewDocNode(doc.get(), NULL, (xmlChar*)"Envelope", NULL);
    xmlDocSetRootElement(doc.get(), root);
    xmlNewProp(root, (xmlChar*)"xmlns:SOAP-ENV", (xmlChar*)"http://www.w3.org/2003/05/soap-envelope");
    xmlNewProp(root, (xmlChar*)"xmlns:a", (xmlChar*)"http://schemas.xmlsoap.org/ws/2004/08/addressing");
    xmlNsPtr ns_env = xmlNewNs(root, NULL, (xmlChar*)"SOAP-ENV");
    xmlNsPtr ns_a = xmlNewNs(root, NULL, (xmlChar*)"a");
    xmlSetNs(root, ns_env);
    xmlNodePtr header = xmlNewTextChild(root, ns_env, (xmlChar*)"Header", NULL);
    xmlNodePtr action = xmlNewTextChild(header, ns_a, (xmlChar*)"Action", (xmlChar*)"http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe");
    xmlNewProp(action, (xmlChar*)"SOAP-ENV:mustUnderstand", (xmlChar*)"1");
    xmlNodePtr messageid = xmlNewTextChild(header, ns_a, (xmlChar*)"MessageID", (xmlChar*)_uuid.c_str());
    xmlNodePtr replyto = xmlNewTextChild(header, ns_a, (xmlChar*)"ReplyTo", NULL);
    xmlNodePtr address = xmlNewTextChild(replyto, ns_a, (xmlChar*)"Address", (xmlChar*)"http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous");
    xmlNodePtr to = xmlNewTextChild(header, ns_a, (xmlChar*)"To", (xmlChar*)"urn:schemas-xmlsoap-org:ws:2005:04:discovery");
    xmlNewProp(to, (xmlChar*)"SOAP-ENV:mustUnderstand", (xmlChar*)"1");
    xmlNodePtr body = xmlNewTextChild(root, ns_env, (xmlChar*)"Body", NULL);
    xmlNodePtr probe = xmlNewTextChild(body, NULL, (xmlChar*)"Probe", NULL);
    xmlNewProp(probe, (xmlChar*)"xmlns:p", (xmlChar*)"http://schemas.xmlsoap.org/ws/2005/04/discovery");
    xmlNsPtr ns_p = xmlNewNs(probe, NULL, (xmlChar*)"p");
    xmlSetNs(probe, ns_p);
    xmlNodePtr types = xmlNewTextChild(probe, NULL, (xmlChar*)"Types", (xmlChar*)"dp0:NetworkVideoTransmitter");
    xmlNewProp(types, (xmlChar*)"xmlns:d", (xmlChar*)"http://schemas.xmlsoap.org/ws/2005/04/discovery");
    xmlNewProp(types, (xmlChar*)"xmlns:dp0", (xmlChar*)"http://www.onvif.org/ver10/network/wsdl");
    xmlNsPtr ns_d = xmlNewNs(types, NULL, (xmlChar*)"d");
    xmlSetNs(types, ns_d);
    raii_ptr<xmlOutputBuffer> outputBuffer(xmlAllocOutputBuffer(NULL), xmlOutputBufferClose);
    xmlNodeDumpOutput(outputBuffer.get(), doc.get(), root, 0, 0, NULL);
    //int size = xmlOutputBufferGetSize(outputBuffer.get());
    string result((char*)xmlOutputBufferGetContent(outputBuffer.get()));
    xmlOutputBufferFlush(outputBuffer.get());

    return result;
}

string r_onvif_session::_get_discovery_xml2() const
{
    return string("<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\"><s:Header><a:Action s:mustUnderstand=\"1\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action><a:MessageID>uuid:6bbdae2d-f229-42c8-a27b-93880fb80826</a:MessageID><a:ReplyTo><a:Address>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:Address></a:ReplyTo><a:To s:mustUnderstand=\"1\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To></s:Header><s:Body><Probe xmlns=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\"><d:Types xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" xmlns:dp0=\"http://www.onvif.org/ver10/device/wsdl\">dp0:Device</d:Types></Probe></s:Body></s:Envelope>");
}

void r_onvif_session::_set_socket_options(
    int fd
) const
{
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000;
    int broadcast = 500;
    char loopch = 0;
    int status = 0;
    struct in_addr localInterface;

#ifdef _WIN32
    PMIB_IPADDRTABLE pIPAddrTable;
    DWORD dwSize = 0;
    DWORD dwRetVal = 0;
    IN_ADDR IPAddr;

    pIPAddrTable = (MIB_IPADDRTABLE *) malloc(sizeof(MIB_IPADDRTABLE));
    if (pIPAddrTable) {
        if (GetIpAddrTable(pIPAddrTable, &dwSize, 0) == ERROR_INSUFFICIENT_BUFFER) {
            free(pIPAddrTable);
            pIPAddrTable = (MIB_IPADDRTABLE *) malloc(dwSize);
        }
        if (pIPAddrTable == NULL) {
            printf("Memory allocation failed for GetIpAddrTable\n");
            return;
        }
    }

    if ((dwRetVal = GetIpAddrTable(pIPAddrTable, &dwSize, 0)) != NO_ERROR) {
        printf("GetIpAddrTable failed with error %lu\n", dwRetVal);
        return;
    }

    int p = 0;
    while (p < (int)pIPAddrTable->dwNumEntries) {
        IPAddr.S_un.S_addr = (u_long)pIPAddrTable->table[p].dwAddr;
        IPAddr.S_un.S_addr = (u_long)pIPAddrTable->table[p].dwMask;
        if (pIPAddrTable->table[p].dwAddr != inet_addr("127.0.0.1") && pIPAddrTable->table[p].dwMask == inet_addr("255.255.255.0")) {
            localInterface.s_addr = pIPAddrTable->table[p].dwAddr;
            status = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&localInterface, sizeof(localInterface));
            if (status < 0)
                printf("ip_multicast_if error");
            p = (int)pIPAddrTable->dwNumEntries;
        }
        p++;
    }

    if (pIPAddrTable) {
        free(pIPAddrTable);
        pIPAddrTable = NULL;
    }

    status = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&broadcast, sizeof(broadcast));
#else
    status = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&tv, sizeof(struct timeval));
#endif
    status = setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopch, sizeof(loopch));
}

r_nullable<string> r_onvif_session::_get_scope_field(
    const string& scope,
    const string& field_name
) const
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

string r_onvif_session::_get_camera_name(
    int index,
    const string& default_name
) const
{
    raii_ptr<xmlDoc> xml_input(xmlParseMemory(_buf[index], _len[index]), xmlFreeDoc);

    auto scopes = _get_xml_value(xml_input.get(), "//s:Body//d:ProbeMatches//d:ProbeMatch//d:Scopes", default_name);

    auto mfgr = _get_scope_field(scopes, (char*)"onvif://www.onvif.org/name/");

    auto hdwr = _get_scope_field(scopes, (char*)"onvif://www.onvif.org/hardware/");

    // SAMSUNG E4500n
    // SAMSUNG 192.168.1.11
    // E4500n 192

    string output;

    if(!mfgr.is_null())
        output = mfgr.value();

    if(!hdwr.is_null())
        output += " " + hdwr.value();
    
    if(output.empty())
        output = default_name;

    return output;
}

std::string r_onvif_session::_extract_xaddrs(
    int index
) const
{
    raii_ptr<xmlDoc> xml_input(xmlParseMemory(_buf[index], _len[index]), [](xmlDoc* p){xmlFreeDoc(p);});

    auto value = _get_xml_value(xml_input.get(), "//s:Body//d:ProbeMatches//d:ProbeMatch//d:XAddrs", string(_buf[index], _len[index]));

    return r_string_utils::split(value, " ").front();
}

std::string r_onvif_session::_extract_address(
    int index
) const
{
    raii_ptr<xmlDoc> xml_input(xmlParseMemory(_buf[index], _len[index]), [](xmlDoc* p){xmlFreeDoc(p);});
                          
    return _get_xml_value(xml_input.get(), "//s:Body//d:ProbeMatches//d:ProbeMatch//a:EndpointReference//a:Address", string(_buf[index], _len[index]));
}

string r_onvif_session::_get_xml_value(
    xmlDocPtr doc,
    const string& xpath,
    const std::string& id
) const
{
    auto value = _maybe_get_xml_value(doc, xpath);
    if(value.is_null())
        R_THROW(("Unable to find XPath: id(%s)", id.c_str()));
    return value.value();
}

bool r_onvif_session::_has_xml_node(xmlDocPtr doc, const std::string& xpath) const
{
    raii_ptr<xmlXPathContext> context(xmlXPathNewContext(doc), xmlXPathFreeContext);
    if(!context.get())
        return false;

    xmlXPathRegisterNs(context.get(), (xmlChar*)"s", (xmlChar*)"http://www.w3.org/2003/05/soap-envelope");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"trt", (xmlChar*)"http://www.onvif.org/ver10/media/wsdl");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"tt", (xmlChar*)"http://www.onvif.org/ver10/schema");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"tds", (xmlChar*)"http://www.onvif.org/ver10/device/wsdl");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"timg", (xmlChar*)"http://www.onvif.org/ver20/imaging/wsdl");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"wsa5", (xmlChar*)"http://www.w3.org/2005/08/addressing");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"wsnt", (xmlChar*)"http://docs.oasis-open.org/wsn/b-2");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"d", (xmlChar*)"http://schemas.xmlsoap.org/ws/2005/04/discovery");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"ter", (xmlChar*)"http://www.onvif.org/ver10/error");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"a", (xmlChar*)"http://schemas.xmlsoap.org/ws/2004/08/addressing");

    raii_ptr<xmlXPathObject> xpo(xmlXPathEvalExpression((xmlChar*)xpath.c_str(), context.get()), xmlXPathFreeObject);
    if(xpo.get() == NULL || xmlXPathNodeSetIsEmpty(xpo.get()->nodesetval))
        return false;
    return true;
}

r_nullable<string> r_onvif_session::_maybe_get_xml_value(
    xmlDocPtr doc,
    const std::string& xpath
) const
{
    r_nullable<string> result;

    raii_ptr<xmlXPathContext> context(xmlXPathNewContext(doc), xmlXPathFreeContext);
    if(!context.get())
        return result;

    xmlXPathRegisterNs(context.get(), (xmlChar*)"s", (xmlChar*)"http://www.w3.org/2003/05/soap-envelope");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"trt", (xmlChar*)"http://www.onvif.org/ver10/media/wsdl");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"tt", (xmlChar*)"http://www.onvif.org/ver10/schema");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"tds", (xmlChar*)"http://www.onvif.org/ver10/device/wsdl");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"timg", (xmlChar*)"http://www.onvif.org/ver20/imaging/wsdl");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"wsa5", (xmlChar*)"http://www.w3.org/2005/08/addressing");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"wsnt", (xmlChar*)"http://docs.oasis-open.org/wsn/b-2");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"d", (xmlChar*)"http://schemas.xmlsoap.org/ws/2005/04/discovery");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"ter", (xmlChar*)"http://www.onvif.org/ver10/error");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"a", (xmlChar*)"http://schemas.xmlsoap.org/ws/2004/08/addressing");

    raii_ptr<xmlXPathObject> xpo(xmlXPathEvalExpression((xmlChar*)xpath.c_str(), context.get()), xmlXPathFreeObject);
    if(xpo.get() == NULL || xmlXPathNodeSetIsEmpty(xpo.get()->nodesetval))
        return result;

    raii_ptr<char> keyword((char*)xmlNodeListGetString(doc, xpo.get()->nodesetval->nodeTab[0]->xmlChildrenNode, 1), xmlFree);
    if(keyword.get() == NULL)
        return result;
    result.set_value(string((char*)keyword.get()));

    return result;
}

string r_onvif_session::_get_node_attribute(
    xmlDocPtr doc,
    const string& xpath,
    const string& attribute
) const
{
    raii_ptr<xmlXPathContext> context(xmlXPathNewContext(doc), xmlXPathFreeContext);
    if(!context.get())
        R_THROW(("Unable to create xml path context."));

    xmlXPathRegisterNs(context.get(), (xmlChar*)"s", (xmlChar*)"http://www.w3.org/2003/05/soap-envelope");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"trt", (xmlChar*)"http://www.onvif.org/ver10/media/wsdl");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"tt", (xmlChar*)"http://www.onvif.org/ver10/schema");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"tds", (xmlChar*)"http://www.onvif.org/ver10/device/wsdl");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"timg", (xmlChar*)"http://www.onvif.org/ver20/imaging/wsdl");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"wsa5", (xmlChar*)"http://www.w3.org/2005/08/addressing");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"wsnt", (xmlChar*)"http://docs.oasis-open.org/wsn/b-2");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"ter", (xmlChar*)"http://www.onvif.org/ver10/error");
    xmlXPathRegisterNs(context.get(), (xmlChar*)"a", (xmlChar*)"http://schemas.xmlsoap.org/ws/2004/08/addressing");

    raii_ptr<xmlXPathObject> result(xmlXPathEvalExpression((xmlChar*)xpath.c_str(), context.get()), xmlXPathFreeObject);
    if(!result.get())
        R_THROW(("Unable to find xpath."));

    raii_ptr<char> val((char*)xmlGetProp(result.get()->nodesetval->nodeTab[0], (xmlChar*)attribute.c_str()), xmlFree);

    return string(val.get());
}

string r_onvif_session::_extract_onvif_service(
    const std::string& service,
    bool post
) const
{
    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(service, host, port, protocol, uri);

    if(post)
        uri = "POST " + uri + " HTTP/1.1\r\n";
    
    return uri;
}

#if 0
int r_onvif_session::_get_time_offset(
    const string& device_service,
    const string& xaddrs
) const
{
    int result = 0;

    raii_ptr<xmlDoc> doc(xmlNewDoc((xmlChar*)"1.0"), xmlFreeDoc);
    xmlNodePtr root = xmlNewDocNode(doc.get(), NULL, (xmlChar*)"Envelope", NULL);
    xmlDocSetRootElement(doc.get(), root);
    xmlNsPtr ns_env = xmlNewNs(root, (xmlChar*)"http://www.w3.org/2003/05/soap-envelope", (xmlChar*)"SOAP-ENV");
    xmlNsPtr ns_tds = xmlNewNs(root, (xmlChar*)"http://www.onvif.org/ver10/device/wsdl", (xmlChar*)"tds");
    xmlSetNs(root, ns_env);
    xmlNodePtr body = xmlNewTextChild(root, ns_env, (xmlChar*)"Body", NULL);
    xmlNewTextChild(body, ns_tds, (xmlChar*)"GetSystemDateAndTime", NULL);
    auto cmd = _add_http_header(doc.get(), root, xaddrs, device_service);

    raii_ptr<xmlDoc> reply(_send_command_to_camera(cmd, xaddrs), xmlFreeDoc);

    if (reply.get() != NULL) {
        auto hour_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Time//tt:Hour");
        auto min_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Time//tt:Minute");
        auto sec_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Time//tt:Second");
        auto year_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Date//tt:Year");
        auto month_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Date//tt:Month");
        auto day_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Date//tt:Day");
        auto dst_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:DaylightSavings");

        auto iso8601 = year_buf + "-" + month_buf + "-" + day_buf + "T" + hour_buf + ":" + min_buf + ":" + sec_buf + "Z";

        auto there_tp = r_time_utils::iso_8601_to_tp(iso8601);

        time_t there_ts = std::chrono::duration_cast<std::chrono::seconds>(there_tp.time_since_epoch()).count();

        time_t here_ts = time(NULL);

        if(there_ts > here_ts)
            result = (int)(there_ts - here_ts);
        else result = -(int)(here_ts - there_ts);

        _check_for_xml_error_msg(reply.get(), xaddrs);
    }
    else R_THROW(("Unable to fetch device time."));

    return result;
}
#endif
#if 1
int r_onvif_session::_get_time_offset(
    const string& device_service,
    const string& xaddrs
) const
{
    int result = 0;

    raii_ptr<xmlDoc> doc(xmlNewDoc((xmlChar*)"1.0"), xmlFreeDoc);
    xmlNodePtr root = xmlNewDocNode(doc.get(), NULL, (xmlChar*)"Envelope", NULL);
    xmlDocSetRootElement(doc.get(), root);
    xmlNsPtr ns_env = xmlNewNs(root, (xmlChar*)"http://www.w3.org/2003/05/soap-envelope", (xmlChar*)"SOAP-ENV");
    xmlNsPtr ns_tds = xmlNewNs(root, (xmlChar*)"http://www.onvif.org/ver10/device/wsdl", (xmlChar*)"tds");
    xmlSetNs(root, ns_env);
    xmlNodePtr body = xmlNewTextChild(root, ns_env, (xmlChar*)"Body", NULL);
    xmlNewTextChild(body, ns_tds, (xmlChar*)"GetSystemDateAndTime", NULL);
    auto cmd = _add_http_header(doc.get(), root, xaddrs, device_service);

    raii_ptr<xmlDoc> reply(_send_command_to_camera(cmd, xaddrs), xmlFreeDoc);

    // If the response has a time, use it
    //     if the time is utc, use it as is
    //     if the time is local, convert it to utc

    // Only parse UTCDateTime if it is present
    //   <tt:UTCDateTime>
    //     <tt:Time>
    //       <tt:Hour>15</tt:Hour>
    //       <tt:Minute>52</tt:Minute>
    //       <tt:Second>25</tt:Second>
    //     </tt:Time>
    //     <tt:Date>
    //       <tt:Year>2010</tt:Year>
    //       <tt:Month>10</tt:Month>
    //       <tt:Day>29</tt:Day>
    //     </tt:Date>
    //   </tt:UTCDateTime>

    // It might also be LocalDataTime
    //  <tt:LocalDateTime>
    //     <tt:Time>
    //       <tt:Hour>15</tt:Hour>
    //       <tt:Minute>52</tt:Minute>
    //       <tt:Second>25</tt:Second>
    //     </tt:Time>
    //     <tt:Date>
    //       <tt:Year>2010</tt:Year>
    //       <tt:Month>10</tt:Month>
    //       <tt:Day>29</tt:Day>
    //     </tt:Date>
    //  </tt:LocalDateTime>

    if (reply.get() != NULL) {
        if (!_has_xml_node(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime"))
            return 0;
        if (!_has_xml_node(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime"))
            return 0;

        auto hour_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Time//tt:Hour", xaddrs);
        auto min_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Time//tt:Minute", xaddrs);
        auto sec_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Time//tt:Second", xaddrs);
        auto year_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Date//tt:Year", xaddrs);
        auto month_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Date//tt:Month", xaddrs);
        auto day_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:UTCDateTime//tt:Date//tt:Day", xaddrs);
        auto dst_buf = _get_xml_value(reply.get(), "//s:Body//tds:GetSystemDateAndTimeResponse//tds:SystemDateAndTime//tt:DaylightSavings", xaddrs);

        int is_dst = (dst_buf == "true") ? 1 : 0;

        time_t now = time(NULL);
#ifdef IS_WINDOWS
        struct tm utc_storage;
        auto err = gmtime_s(&utc_storage, &now);
        if(err != 0)
            R_THROW(("Unable to get UTC time."));
        struct tm* utc_here = &utc_storage;
#endif
#ifdef IS_LINUX
        struct tm *utc_here = gmtime(&now);
        if(utc_here == NULL)
            R_THROW(("Unable to get UTC time."));
#endif
        utc_here->tm_isdst = is_dst;
        time_t utc_time_here = mktime(utc_here);

#ifdef IS_WINDOWS
        err = localtime_s(&utc_storage, &now);
        if(err != 0)
            R_THROW(("Unable to get UTC time."));
        struct tm* utc_there = &utc_storage;
#endif
#ifdef IS_LINUX
        struct tm *utc_there = localtime(&now);
        if(utc_there == NULL)
            R_THROW(("Unable to get UTC time."));
#endif
        utc_there->tm_year = stoi(year_buf) - 1900;
        utc_there->tm_mon = stoi(month_buf) - 1;
        utc_there->tm_mday = stoi(day_buf);
        utc_there->tm_hour = stoi(hour_buf);
        utc_there->tm_min = stoi(min_buf);
        utc_there->tm_sec = stoi(sec_buf);
        utc_there->tm_isdst = is_dst;
        time_t utc_time_there = mktime(utc_there);
        // if utc_time_there > utc_time_here then the camera is in the future so the offset
        // is utc_time_there - utc_time_here
        // if utc_time_there < utc_time_here then the camera is in the past so the offset
        // is utc_time_here - utc_time_there
        int64_t tmp = (int64_t)utc_time_there - (int64_t)utc_time_here;
        
        result = (int)tmp;

        _check_for_xml_error_msg(reply.get(), xaddrs);
    }
    else R_THROW(("Unable to fetch device time."));

    return result;
}
#endif
xmlDocPtr r_onvif_session::_send_command_to_camera(
    const string& cmd,
    const string& xaddrs,
    bool throw_on_unauthorized
) const
{
    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(xaddrs, host, port, protocol, uri);

    r_socket sok;
    sok.connect(host, port);
    sok.set_io_timeout(10000);

    sok.send(cmd.c_str(), cmd.length());

    r_http::r_client_response response;
    response.read_response(sok);

    auto status = response.get_status();
    if(status == r_http::response_unauthorized)
        R_STHROW(r_unauthorized_exception, ("Unauthorized from: %s", xaddrs.c_str()));

    auto maybe_response_body = response.get_body_as_string();

    // if its is_failure() && contains "NotAuthorized" then throw unauthorized!
    if(response.is_failure())
    {
        if(!maybe_response_body.is_null())
        {
            auto response_body = maybe_response_body.value();

            if(r_string_utils::contains(response_body, "NotAuthorized"))
                R_STHROW(r_unauthorized_exception, ("Unauthorized from: %s", xaddrs.c_str()));
        }

        R_THROW(("Error returned from camera: %s", xaddrs.c_str()));
    }

    auto response_body = maybe_response_body.value();
 
    return xmlParseMemory(response_body.c_str(), (int)response_body.size());
}

string r_onvif_session::_add_http_header(
    xmlDocPtr doc,
    xmlNodePtr root,
    const string& xaddrs,
    const string& post_type
) const
{
    xmlOutputBufferPtr outputbuffer = xmlAllocOutputBuffer(NULL);
    xmlNodeDumpOutput(outputbuffer, doc, root, 0, 0, NULL);
    int size = (int)xmlOutputBufferGetSize(outputbuffer);

    char xml[8192] = {0};
    if (size > 8191) {
        fprintf(stderr, "xmlOutputBufferGetSize too big %d\n", size);
#ifdef IS_WINDOWS
        strncat_s(xml, 8192, (char*)xmlOutputBufferGetContent(outputbuffer), 8191);
#endif
#ifdef IS_LINUX
        strncat(xml, (char*)xmlOutputBufferGetContent(outputbuffer), 8191);
#endif
    }
    else {
#ifdef IS_WINDOWS
        strcpy_s(xml, 8192, (char*)xmlOutputBufferGetContent(outputbuffer));
#endif
#ifdef IS_LINUX
        strcpy(xml, (char*)xmlOutputBufferGetContent(outputbuffer));
#endif
    }

    xmlOutputBufferFlush(outputbuffer);
    xmlOutputBufferClose(outputbuffer);

    char c_xml_size[6] = {0, 0, 0, 0, 0, 0};
#ifdef IS_WINDOWS
    sprintf_s(c_xml_size, 6, "%d", size);
#endif
#ifdef IS_LINUX
    sprintf(c_xml_size, "%d", size);
#endif
    int xml_size_length = (int)strlen(c_xml_size)+1;

    string host, protocol, uri;
    int port;
    r_http::parse_url_parts(xaddrs, host, port, protocol, uri);

    char content[] =
    "User-Agent: Generic\r\n"
    "Connection: Close\r\n"
    "Accept-Encoding: gzip, deflate\r\n"
    "Content-Type: application/soap+xml; charset=utf-8;\r\n"
    "Host: ";
    char content_length[] = "\r\nContent-Length: ";

    char http_terminate[5];
    http_terminate[0] = '\r';
    http_terminate[1] = '\n';
    http_terminate[2] = '\r';
    http_terminate[3] = '\n';
    http_terminate[4] = '\0';

    int p = (int)(post_type.size() + 1);
    int h = (int)(host.size() + 1);
    int c = sizeof(content);
    int cl = sizeof(content_length);
    int cmd_size = p + c + h + cl + xml_size_length + size + 1;
    int i;
    int s;
    char cmd[4096];
    memset(cmd, 0, sizeof(cmd));
    for (i=0; i<p-1; i++)
        cmd[i] = post_type[i];
    s = i;
    for (i=0; i<c-1; i++)
        cmd[s+i] = content[i];
    s = s+i;
    for (i=0; i<h-1; i++)
        cmd[s+i] = host[i];
    s = s+i;
    for (i=0; i<cl-1; i++)
        cmd[s+i] = content_length[i];
    s = s+i;
    for (i=0; i<xml_size_length-1; i++)
        cmd[s+i] = c_xml_size[i];
    s = s+i;
    for (i=0; i<5-1; i++)
        cmd[s+i] = http_terminate[i];
    s = s+i;
    for (i=0; i<size; i++)
        cmd[s+i] = xml[i];
    cmd[cmd_size] = '\0';

    return string(cmd);
}

xmlDocPtr r_onvif_session::_send_get_capabilities(
    const r_utils::r_nullable<std::string> username,
    const r_utils::r_nullable<std::string> password,
    const string& xaddrs,
    int time_offset
) const
{
    raii_ptr<xmlDoc> doc(xmlNewDoc((xmlChar*)"1.0"), xmlFreeDoc);
    xmlNodePtr root = xmlNewDocNode(doc.get(), NULL, (xmlChar*)"Envelope", NULL);
    xmlDocSetRootElement(doc.get(), root);
    xmlNsPtr ns_env = xmlNewNs(root, (xmlChar*)"http://www.w3.org/2003/05/soap-envelope", (xmlChar*)"SOAP-ENV");
    xmlNsPtr ns_tds = xmlNewNs(root, (xmlChar*)"http://www.onvif.org/ver10/device/wsdl", (xmlChar*)"tds");
    xmlSetNs(root, ns_env);
    if(!username.is_null())
        add_username_digest_header(root, ns_env, (char*)username.value().c_str(), (char*)password.value().c_str(), time_offset);
    xmlNodePtr body = xmlNewTextChild(root, ns_env, (xmlChar*)"Body", NULL);
    xmlNodePtr capabilities = xmlNewTextChild(body, ns_tds, (xmlChar*)"GetCapabilities", NULL);
    xmlNewTextChild(capabilities, ns_tds, (xmlChar*)"Category", (const xmlChar *)"All");

    auto onvif_service = _extract_onvif_service(xaddrs, true);

    auto cmd = _add_http_header(doc.get(), root, xaddrs, onvif_service);

    return _send_command_to_camera(cmd, xaddrs);
}

void r_onvif_session::_check_for_xml_error_msg(
    xmlDocPtr doc,
    const std::string& xaddr
) const
{
    auto err = _maybe_get_xml_value(doc, "//s:Body//s:Fault//s:Code//s:Subcode//s:Value");
    if(!err.is_null())
        R_THROW(("XML Parser Error: %s %s", xaddr.c_str(), err.value().c_str()));

    err = _maybe_get_xml_value(doc, "//s:Body//s:Fault//s:Reason//s:Text");
    if(!err.is_null())
        R_THROW(("XML Parser Error: %s %s", xaddr.c_str(), err.value().c_str()));
}

string r_onvif_session::_get_first_profile_token(
    const r_nullable<string>& username,
    const r_nullable<string>& password,
    const string& xaddrs,
    const string& media_service,
    int time_offset
) const
{
    int result = 0;
    raii_ptr<xmlDoc> doc(xmlNewDoc((xmlChar*)"1.0"), xmlFreeDoc);
    xmlNodePtr root = xmlNewDocNode(doc.get(), NULL, (xmlChar*)"Envelope", NULL);
    xmlDocSetRootElement(doc.get(), root);
    xmlNsPtr ns_env = xmlNewNs(root, (xmlChar*)"http://www.w3.org/2003/05/soap-envelope", (xmlChar*)"SOAP-ENV");
    xmlNsPtr ns_trt = xmlNewNs(root, (xmlChar*)"http://www.onvif.org/ver10/media/wsdl", (xmlChar*)"trt");
    xmlSetNs(root, ns_env);
    if(!username.is_null())
        add_username_digest_header(root, ns_env, username.value().c_str(), password.value().c_str(), time_offset);
    xmlNodePtr body = xmlNewTextChild(root, ns_env, (xmlChar*)"Body", NULL);
    xmlNewTextChild(body, ns_trt, (xmlChar*)"GetProfiles", NULL);

    auto cmd = _add_http_header(doc.get(), root, xaddrs, media_service);

    raii_ptr<xmlDoc> reply(_send_command_to_camera(cmd, xaddrs), xmlFreeDoc);

    if(!reply.get())
        R_THROW(("Unable to read reply from camera"));

    _check_for_xml_error_msg(reply.get(), xaddrs);

    return _get_node_attribute(reply.get(), "//s:Body//trt:GetProfilesResponse//trt:Profiles", "token");
}

string r_onvif_session::_get_stream_uri(
    const r_nullable<string>& username,
    const r_nullable<string>& password,
    int timeOffset,
    string& profileToken,
    const string& xaddrs,
    const string& mediaService
) const
{
    int result = 0;
    raii_ptr<xmlDoc> doc(xmlNewDoc((xmlChar*)"1.0"), xmlFreeDoc);
    xmlNodePtr root = xmlNewDocNode(doc.get(), NULL, (xmlChar*)"Envelope", NULL);
    xmlDocSetRootElement(doc.get(), root);
    xmlNsPtr ns_env = xmlNewNs(root, (xmlChar*)"http://www.w3.org/2003/05/soap-envelope", (xmlChar*)"SOAP-ENV");
    xmlNsPtr ns_trt = xmlNewNs(root, (xmlChar*)"http://www.onvif.org/ver10/media/wsdl", (xmlChar*)"trt");
    xmlNsPtr ns_tt = xmlNewNs(root, (xmlChar*)"http://www.onvif.org/ver10/schema", (xmlChar*)"tt");
    xmlSetNs(root, ns_env);
    if(!username.is_null())
        add_username_digest_header(root, ns_env, (char*)username.value().c_str(), (char*)password.value().c_str(), timeOffset);
    xmlNodePtr body = xmlNewTextChild(root, ns_env, (xmlChar*)"Body", NULL);
    xmlNodePtr getStreamUri = xmlNewTextChild(body, ns_trt, (xmlChar*)"GetStreamUri", NULL);
    xmlNodePtr streamSetup = xmlNewTextChild(getStreamUri, ns_trt, (xmlChar*)"StreamSetup", NULL);
    xmlNewTextChild(streamSetup, ns_tt, (xmlChar*)"Stream", (xmlChar*)"RTP-Unicast");
    xmlNodePtr transport = xmlNewTextChild(streamSetup, ns_tt, (xmlChar*)"Transport", NULL);
    xmlNewTextChild(transport, ns_tt, (xmlChar*)"Protocol", (xmlChar*)"RTSP");
    xmlNewTextChild(getStreamUri, ns_trt, (xmlChar*)"ProfileToken", (xmlChar*)profileToken.c_str());

    auto cmd = _add_http_header(doc.get(), root, xaddrs, mediaService);
    raii_ptr<xmlDoc> reply(_send_command_to_camera(cmd, xaddrs), xmlFreeDoc);

    if(!reply.get())
        R_THROW(("Unable to read reply from camera"));
    
    _check_for_xml_error_msg(reply.get(), xaddrs);

    return _get_xml_value(reply.get(), "//s:Body//trt:GetStreamUriResponse//trt:MediaUri//tt:Uri", xaddrs);
}

xmlDocPtr r_onvif_session::_send_get_device_information(
    const r_nullable<string>& username,
    const r_nullable<string>& password,
    int timeOffset,
    const string& xaddrs,
    const string& device_service
) const
{
    int result = 0;
    raii_ptr<xmlDoc> doc(xmlNewDoc((xmlChar*)"1.0"), xmlFreeDoc);
    xmlNodePtr root = xmlNewDocNode(doc.get(), NULL, (xmlChar*)"Envelope", NULL);
    xmlDocSetRootElement(doc.get(), root);
    xmlNsPtr ns_env = xmlNewNs(root, (xmlChar*)"http://www.w3.org/2003/05/soap-envelope", (xmlChar*)"SOAP-ENV");
    xmlNsPtr ns_tds = xmlNewNs(root, (xmlChar*)"http://www.onvif.org/ver10/device/wsdl", (xmlChar*)"tds");
    xmlSetNs(root, ns_env);
    if(!username.is_null())
        add_username_digest_header(root, ns_env, (char*)username.value().c_str(), (char*)password.value().c_str(), timeOffset);
    xmlNodePtr body = xmlNewTextChild(root, ns_env, (xmlChar*)"Body", NULL);
    xmlNewTextChild(body, ns_tds, (xmlChar*)"GetDeviceInformation", NULL);

    return _send_command_to_camera(_add_http_header(doc.get(), root, xaddrs, device_service), xaddrs);
}

void r_onvif_session::add_username_digest_header(
    xmlNodePtr root,
    xmlNsPtr ns_env,
    const char *user,
    const char *password,
    time_t offset
) const
{
    srand((unsigned int)time(NULL));

#ifdef _WIN32
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

    auto b64_encoded = r_string_utils::to_base64(nonce_buffer, nonce_chunk_size);
    memcpy(nonce_result, b64_encoded.c_str(), b64_encoded.length());

#ifdef IS_WINDOWS
    strcpy_s(nonce_base64, 1024, (char*)nonce_result);
#endif
#ifdef IS_LINUX
    strcpy(nonce_base64, (char*)nonce_result);
#endif

    auto now = chrono::system_clock::now();
    auto delta = chrono::duration_cast<chrono::milliseconds>(now.time_since_epoch());

    struct timeval tv;
    tv.tv_sec = (long)(delta.count() / 1000);
    tv.tv_usec = (delta.count() % 1000) * 1000;

    int millisec = tv.tv_usec / 1000;

    char time_buffer[1024];
#ifdef IS_WINDOWS
    struct tm tm_storage;
    time_t then = tv.tv_sec + offset;
    auto err = gmtime_s(&tm_storage, &then);
    if(err != 0)
        R_THROW(("gmtime_s failed"));
    struct tm* this_tm = &tm_storage;
#endif
#ifdef IS_LINUX
    time_t then = tv.tv_sec + offset;
    struct tm* this_tm = gmtime((time_t*)&then);
#endif
    size_t time_buffer_length = strftime(time_buffer, 1024, "%Y-%m-%dT%H:%M:%S.", this_tm);
    time_buffer[time_buffer_length] = '\0';

    char milli_buf[16] = {0};
#ifdef IS_WINDOWS
    sprintf_s(milli_buf, 16, "%03dZ", millisec);
#endif
#ifdef IS_LINUX
    sprintf(milli_buf, "%03dZ", millisec);
#endif

#ifdef IS_WINDOWS
    strcat_s(time_buffer, 1024, milli_buf);
#endif
#ifdef IS_LINUX
    strcat(time_buffer, milli_buf);
#endif

    r_sha1 ctx;
    ctx.update(nonce_buffer, nonce_chunk_size);
    ctx.update((const unsigned char*)time_buffer, strlen(time_buffer));
    ctx.update((const unsigned char*)password, strlen(password));
    ctx.finalize();

    unsigned char hash[20];
    ctx.get(&hash[0]);

    unsigned int digest_chunk_size = 20;
    unsigned char digest_result[128];
    b64_encoded = r_string_utils::to_base64(&hash[0], digest_chunk_size);
    memset(digest_result, 0, 128);
    memcpy(digest_result, b64_encoded.c_str(), b64_encoded.length());

#ifdef IS_WINDOWS
    strcpy_s(time_holder, 1024, time_buffer);
    strcpy_s(digest_base64, 1024, (char*)digest_result);
#endif
#ifdef IS_LINUX
    strcpy(time_holder, time_buffer);
    strcpy(digest_base64, (const char *)digest_result);
#endif

    xmlNsPtr ns_wsse = xmlNewNs(root, (xmlChar*)"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd", (xmlChar*)"wsse");
    xmlNsPtr ns_wsu = xmlNewNs(root, (xmlChar*)"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd", (xmlChar*)"wsu");
    xmlNodePtr header = xmlNewTextChild(root, ns_env, (xmlChar*)"Header", NULL);
    xmlNodePtr security = xmlNewTextChild(header, ns_wsse, (xmlChar*)"Security", NULL);
    xmlNewProp(security, (xmlChar*)"SOAP-ENV:mustUnderstand", (xmlChar*)"1");
    xmlNodePtr username_token = xmlNewTextChild(security, ns_wsse, (xmlChar*)"UsernameToken", NULL);
    xmlNewTextChild(username_token, ns_wsse, (xmlChar*)"Username", (xmlChar*)user);
    xmlNodePtr pwd = xmlNewTextChild(username_token, ns_wsse, (xmlChar*)"Password", (xmlChar*)digest_base64);
    xmlNewProp(pwd, (xmlChar*)"Type", (xmlChar*)"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest");
    xmlNodePtr nonce = xmlNewTextChild(username_token, ns_wsse, (xmlChar*)"Nonce", (xmlChar*)nonce_base64);
    xmlNewProp(nonce, (xmlChar*)"EncodingType", (xmlChar*)"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary");
    xmlNewTextChild(username_token, ns_wsu, (xmlChar*)"Created", (xmlChar*)time_holder);
}
