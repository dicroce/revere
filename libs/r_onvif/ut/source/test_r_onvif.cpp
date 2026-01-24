
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
