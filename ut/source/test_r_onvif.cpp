
#include "test_r_onvif.h"
#include "r_onvif/r_onvif_session.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_sha1.h"
#include <string.h>
#include <map>

using namespace std;
using namespace r_onvif;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_onvif);

void test_r_onvif::setup()
{
}

void test_r_onvif::teardown()
{
}

void test_r_onvif::test_r_onvif_session_basic()
{
    struct keys
    {
        string username;
        string password;
    };

    map<string, keys> key_map;

    {
        // reolink rlc810a
        keys k;
        k.username = "admin";
        k.password = "emperor1";
        key_map.insert(make_pair("urn:uuid:2419d68a-2dd2-21b2-a205-ec:71:db:16:b1:12",k));
    }

    {
        // axis m-3075
        keys k;
        k.username = "root";
        k.password = "emperor1";
        key_map.insert(make_pair("urn:uuid:e58f6613-b3bf-4aa0-90da-250d77bb2fda",k));
    }

    {
        // Dahua 1A404XBNR
        keys k;
        k.username = "admin";
        k.password = "emperor1";
        key_map.insert(make_pair("uuid:b1fd91c3-9f24-b6e7-2a5c-b878404a9f24",k));
    }

    {
        // Hanhua Wisenet QNO-6082R 
        keys k;
        k.username = "admin";
        k.password = "nX760x2904SY";
        key_map.insert(make_pair("urn:uuid:5a4d5857-3656-3452-3330-3032304d4100",k));
    }

    {
        // Evolution12 EVO-12NxD
        keys k;
        k.username = "dicroce";
        k.password = "emperor1";
        key_map.insert(make_pair("urn:uuid:1419d68a-1dd2-11b2-a105-00113503015F",k));
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
}
