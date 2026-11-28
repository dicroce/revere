#include "test_r_camera_kind.h"
#include "r_vss/r_query.h"
#include "r_disco/r_devices.h"
#include "r_http/r_http_exception.h"
#include "r_utils/r_exception.h"
#include <filesystem>

using namespace r_vss;
using namespace r_disco;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_camera_kind);

// /update_camera_properties' "kind": set when given, left alone when not, refused when it isn't one.
void test_r_camera_kind::test_update_properties_sets_kind()
{
    auto dir = (filesystem::temp_directory_path() / "r_camera_kind_test").string();
    filesystem::remove_all(dir);
    filesystem::create_directories(dir);
    {
        r_devices devices(dir);
        devices.start();
        r_stream_config sc;
        sc.id = "cam-1";
        sc.ipv4 = "10.0.0.5";
        sc.rtsp_url = "rtsp://10.0.0.5/stream1";
        sc.model = "Reolink Video Doorbell PoE";
        devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
        auto kind_of = [&]() { return devices.get_camera_by_id("cam-1").value().kind; };

        // Motion detection stays off: this camera has no recording to put motion files beside.
        query_update_camera_properties(dir, devices, "cam-1", false, false, 24);
        RTF_ASSERT(kind_of().is_null());

        query_update_camera_properties(dir, devices, "cam-1", false, false, 24, r_nullable<string>(), string("camera"));
        RTF_ASSERT(kind_of().value() == "camera");
        query_update_camera_properties(dir, devices, "cam-1", false, false, 24, string("2"));
        RTF_ASSERT(kind_of().value() == "camera");     // not given: kept
        query_update_camera_properties(dir, devices, "cam-1", false, false, 24, r_nullable<string>(), string("doorbell"));
        RTF_ASSERT(kind_of().value() == "doorbell");

        for(auto bad : {"", "Doorbell", "sensor"})
        {
            bool refused = false;
            try { query_update_camera_properties(dir, devices, "cam-1", false, false, 24, r_nullable<string>(), string(bad)); }
            catch(const r_http::r_http_400_exception&) { refused = true; }
            RTF_ASSERT(refused);
        }
        RTF_ASSERT(kind_of().value() == "doorbell");    // a refused change changes nothing

        // The press source: set, kept when not given, and only onvif or reolink_email.
        auto source_of = [&]() { return devices.get_camera_by_id("cam-1").value().press_source; };
        RTF_ASSERT(source_of().is_null());
        query_update_camera_properties(dir, devices, "cam-1", false, false, 24, r_nullable<string>(), r_nullable<string>(), string("reolink_email"));
        RTF_ASSERT(source_of().value() == "reolink_email");
        query_update_camera_properties(dir, devices, "cam-1", false, false, 24, string("2"));
        RTF_ASSERT(source_of().value() == "reolink_email");
        for(auto bad : {"", "email", "ONVIF"})
        {
            bool refused = false;
            try { query_update_camera_properties(dir, devices, "cam-1", false, false, 24, r_nullable<string>(), r_nullable<string>(), string(bad)); }
            catch(const r_http::r_http_400_exception&) { refused = true; }
            RTF_ASSERT(refused);
        }
        query_update_camera_properties(dir, devices, "cam-1", false, false, 24, r_nullable<string>(), r_nullable<string>(), string("onvif"));
        RTF_ASSERT(source_of().value() == "onvif");

        bool not_found = false;
        try { query_update_camera_properties(dir, devices, "no-such", false, false, 24, r_nullable<string>(), string("doorbell")); }
        catch(const r_not_found_exception&) { not_found = true; }
        RTF_ASSERT(not_found);
        devices.stop();
    }
    filesystem::remove_all(dir);
}
