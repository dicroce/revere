
#include "test_r_onvif.h"
#include "r_onvif/r_onvif_session.h"
#include "r_onvif/r_onvif_cam.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_sha1.h"
#include "r_utils/r_uuid.h"
#include "r_utils/r_socket.h"
#include <string.h>
#include <map>

using namespace std;
using namespace r_onvif;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_onvif);

void test_r_onvif::setup()
{
    r_raw_socket::socket_startup();
}

void test_r_onvif::teardown()
{
}

void test_r_onvif::test_r_onvif_session_basic()
{
    auto discovered = r_onvif::discover(r_uuid::generate());

    auto filtered = r_onvif::filter_discovered(discovered);

    for(auto& di : filtered)
    {
        printf("%s\n", di.camera_name.c_str());
        r_nullable<string> username, password;
        username = "root";
        password = "emperor1";
        r_onvif_cam_caps cam(di.host, di.port, di.protocol, di.uri, username, password);

        auto caps = cam.get_camera_capabilities();

        auto oms = cam.get_media_service(caps);

        printf("onvif media service url=%s\n", oms.c_str());

        auto profile_tokens = cam.get_profile_tokens(oms);

        for(auto& pt : profile_tokens)
        {
            printf("profile_token=%s, encoding=%s, width=%d, height=%d\n", pt.token.c_str(), pt.encoding.c_str(), pt.width, pt.height);
        }










    }

//    r_onvif_session session;

//    auto discovered2= session.discover();

//    printf("discovered2.size()=%ld\n", discovered2.size());

//    bool foundSomething = false;
//    for(auto& di : discovered2)
//    {
//    }

#if 0
    struct keys
    {
        string username;
        string password;
    };

    map<string, keys> key_map;

    {
        // reolink rlc810a
        keys k;
        k.username = "login";
        k.password = "password";
        key_map.insert(make_pair("urn:uuid:2419d68a-2dd2-21b2-a205-ec:71:db:16:b1:12",k));
    }

    {
        // axis m-3075
        keys k;
        k.username = "login";
        k.password = "password";
        key_map.insert(make_pair("urn:uuid:e58f6613-b3bf-4aa0-90da-250d77bb2fda",k));
    }

    r_onvif_session session;

    auto discovered = session.discover();

    bool foundSomething = false;
    for(auto& di : discovered)
    {
        if(key_map.find(di.address) != key_map.end())
        {
            auto keys = key_map.at(di.address);

            auto rdi = session.get_rtsp_url(
                di.camera_name,
                di.ipv4,
                di.xaddrs,
                di.address,
                keys.username,
                keys.password
            );

            if(!rdi.is_null())
            {
                foundSomething = true;
                RTF_ASSERT(rdi.value().rtsp_url.find("rtsp://") != string::npos);
                printf("camera_name=%s, rtsp_url=%s\n", di.camera_name.c_str(), rdi.value().rtsp_url.c_str());
                fflush(stdout);
            }
        }
    }

    RTF_ASSERT(foundSomething);
#endif
}

void test_r_onvif::test_r_onvif_event_capabilities()
{
    // Discover ONVIF cameras
    auto discovered = r_onvif::discover(r_uuid::generate());
    auto filtered = r_onvif::filter_discovered(discovered);

    printf("\n=== ONVIF Event Capabilities Test ===\n");
    printf("Found %zu cameras\n\n", filtered.size());

    for(auto& di : filtered)
    {
        printf("Camera: %s (%s:%d)\n", di.camera_name.c_str(), di.host.c_str(), di.port);

        r_nullable<string> username, password;
        // IPC-BO cameras use admin/emperor1, AXIS cameras use root/emperor1
        if(di.camera_name.find("IPC-BO") != string::npos)
        {
            username = "admin";
            password = "emperor1";
        }
        else
        {
            username = "root";
            password = "emperor1";
        }

        try
        {
            // First, let's directly test getting the system date/time
            printf("  Testing GetSystemDateAndTime...\n");
            r_onvif_cam_caps caps_obj(di.host, di.port, di.protocol, di.uri, username, password);
            printf("  Time offset: %d seconds\n", caps_obj.get_time_offset_seconds());

            // Create r_onvif_cam instance
            r_onvif_cam cam(di.host, di.port, di.protocol, di.uri, username, password);

            printf("  Querying event capabilities...\n");

            // Get event capabilities
            auto caps = cam.get_event_capabilities();

            bool has_motion_alarm = has_capability(caps, event_capability::motion_alarm);
            bool has_cell_motion = has_capability(caps, event_capability::cell_motion_detector);

            printf("  Event Capabilities:\n");
            printf("    - motion_alarm: %s\n", has_motion_alarm ? "YES" : "NO");
            printf("    - cell_motion_detector: %s\n", has_cell_motion ? "YES" : "NO");
            printf("  Supports motion events: %s\n", (has_motion_alarm || has_cell_motion) ? "YES" : "NO");

            // Also test the supports_motion_events() method
            bool supports = cam.supports_motion_events();
            printf("  supports_motion_events() returns: %s\n", supports ? "true" : "false");
        }
        catch(const exception& e)
        {
            printf("  ERROR: %s\n", e.what());
        }

        printf("\n");
    }

    printf("=== Event Capabilities Test Complete ===\n");
}

void test_r_onvif::test_r_onvif_raw_datetime()
{
    // Discover ONVIF cameras
    auto discovered = r_onvif::discover(r_uuid::generate());
    auto filtered = r_onvif::filter_discovered(discovered);

    printf("\n=== Raw GetSystemDateAndTime Test ===\n");
    printf("Found %zu cameras\n\n", filtered.size());

    for(auto& di : filtered)
    {
        printf("Camera: %s (%s:%d%s)\n", di.camera_name.c_str(), di.host.c_str(), di.port, di.uri.c_str());

        try
        {
            // Build a simple GetSystemDateAndTime request (no auth required for this call)
            string soap_body = R"(<?xml version="1.0" encoding="UTF-8"?>
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://www.w3.org/2003/05/soap-envelope" xmlns:tds="http://www.onvif.org/ver10/device/wsdl">
  <SOAP-ENV:Body>
    <tds:GetSystemDateAndTime/>
  </SOAP-ENV:Body>
</SOAP-ENV:Envelope>)";

            r_utils::r_socket sock;
            sock.connect(di.host, di.port);

            r_http::r_client_request request(di.host, di.port);
            request.set_method(r_http::METHOD_POST);
            request.set_uri(di.uri);
            request.set_content_type("application/soap+xml; charset=utf-8");
            request.set_body(soap_body);

            request.write_request(sock);

            r_http::r_client_response response;
            response.read_response(sock);

            printf("  Status: %d\n", response.get_status());

            auto maybe_body = response.get_body_as_string();
            if(!maybe_body.is_null())
            {
                printf("  Response:\n%s\n", maybe_body.value().c_str());
            }
            else
            {
                printf("  (no body)\n");
            }
        }
        catch(const exception& e)
        {
            printf("  ERROR: %s\n", e.what());
        }

        printf("\n");
    }

    printf("=== Raw DateTime Test Complete ===\n");
}

void test_r_onvif::test_r_onvif_raw_capabilities()
{
    // Test just the AXIS cameras with raw GetCapabilities
    printf("\n=== Raw GetCapabilities Test (AXIS only) ===\n");

    // Hardcode AXIS camera for testing
    string host = "192.168.1.30";
    int port = 80;
    string uri = "/onvif/device_service";

    printf("Camera: AXIS M3075-V (%s:%d%s)\n", host.c_str(), port, uri.c_str());

    try
    {
        // Try SOAP 1.2 envelope (what we normally use)
        string soap_body = R"(<?xml version="1.0" encoding="UTF-8"?>
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://www.w3.org/2003/05/soap-envelope" xmlns:tds="http://www.onvif.org/ver10/device/wsdl">
  <SOAP-ENV:Body>
    <tds:GetCapabilities>
      <tds:Category>All</tds:Category>
    </tds:GetCapabilities>
  </SOAP-ENV:Body>
</SOAP-ENV:Envelope>)";

        r_utils::r_socket sock;
        sock.connect(host, port);

        r_http::r_client_request request(host, port);
        request.set_method(r_http::METHOD_POST);
        request.set_uri(uri);
        request.set_content_type("application/soap+xml; charset=utf-8");
        request.set_body(soap_body);

        request.write_request(sock);

        r_http::r_client_response response;
        response.read_response(sock);

        printf("  SOAP 1.2 Status: %d\n", response.get_status());

        auto maybe_body = response.get_body_as_string();
        if(!maybe_body.is_null())
        {
            // Print first 2000 chars
            string body = maybe_body.value();
            if(body.length() > 2000)
                body = body.substr(0, 2000) + "...";
            printf("  Response:\n%s\n", body.c_str());
        }
    }
    catch(const exception& e)
    {
        printf("  ERROR: %s\n", e.what());
    }

    printf("\n=== Raw Capabilities Test Complete ===\n");
}
