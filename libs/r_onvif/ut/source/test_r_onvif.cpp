
#include "test_r_onvif.h"
#include "r_onvif/r_onvif_session.h"
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
        r_onvif_cam cam(di.host, di.port, di.protocol, di.uri, username, password);

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

// A stop request ends a probe before it sends anything, so a shutdown never
// waits on a send or a receive timeout (r_agent::stop() joins this thread).
void test_r_onvif::test_r_onvif_probes_stop_at_once()
{
    auto stopped = []{ return false; };

    auto started = std::chrono::steady_clock::now();
    auto unicast = r_onvif::discover_unicast(r_uuid::generate(), {"127.0.0.1", "127.0.0.2", "127.0.0.3"}, stopped);
    auto unicast_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    RTF_ASSERT(unicast.empty());
    RTF_ASSERT(unicast_ms < 500);

    started = std::chrono::steady_clock::now();
    auto multicast = r_onvif::discover(r_uuid::generate(), stopped);
    auto multicast_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    RTF_ASSERT(multicast.empty());
    RTF_ASSERT(multicast_ms < 500);
}

namespace
{
std::string probe_match(const std::string& xaddrs, const std::string& address = "urn:uuid:11111111-2222-3333-4444-555555555555")
{
    return
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\""
        " xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\"><s:Body><d:ProbeMatches><d:ProbeMatch>"
        "<a:EndpointReference><a:Address>" + address + "</a:Address></a:EndpointReference>"
        "<d:Scopes>onvif://www.onvif.org/name/Porch</d:Scopes>"
        "<d:XAddrs>" + xaddrs + "</d:XAddrs>"
        "</d:ProbeMatch></d:ProbeMatches></s:Body></s:Envelope>";
}
}

// A reply is used only for services on the host that sent it: one naming
// another host's address is dropped.
void test_r_onvif::test_discovery_uses_only_the_senders_services()
{
    auto own = filter_discovered({{"10.0.0.5", probe_match("http://10.0.0.5/onvif/device_service")}});
    RTF_ASSERT(own.size() == 1);
    RTF_ASSERT(own[0].host == "10.0.0.5");

    auto other = filter_discovered({{"10.0.0.66", probe_match("http://10.0.0.5/onvif/device_service")}});
    RTF_ASSERT(other.empty());

    // Of several addresses, only the sender's.
    auto mixed = filter_discovered({{"10.0.0.5", probe_match("http://10.0.0.99/onvif/device_service http://10.0.0.5:8000/onvif/device_service")}});
    RTF_ASSERT(mixed.size() == 1);
    RTF_ASSERT(mixed[0].host == "10.0.0.5" && mixed[0].port == 8000);

    auto unknown_sender = filter_discovered({{"", probe_match("http://10.0.0.5/onvif/device_service")}});
    RTF_ASSERT(unknown_sender.empty());
}

void test_r_onvif::test_redirects_stay_on_host()
{
    RTF_ASSERT(redirect_stays_on_host("10.0.0.5", "/onvif/device_service"));
    RTF_ASSERT(redirect_stays_on_host("10.0.0.5", "device_service"));
    RTF_ASSERT(redirect_stays_on_host("10.0.0.5", "http://10.0.0.5:8080/onvif/device_service"));
    RTF_ASSERT(redirect_stays_on_host("Camera.local", "https://camera.LOCAL/onvif/device_service"));
    RTF_ASSERT(!redirect_stays_on_host("10.0.0.5", "http://10.0.0.66/onvif/device_service"));
    RTF_ASSERT(!redirect_stays_on_host("10.0.0.5", "http://10.0.0.5.example.com/x"));
    RTF_ASSERT(!redirect_stays_on_host("10.0.0.5", "http:///x"));
}
