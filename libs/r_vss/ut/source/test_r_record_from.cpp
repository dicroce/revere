#include "test_r_record_from.h"
#include "r_vss/r_query.h"
#include "r_disco/r_devices.h"
#include "r_http/r_http_exception.h"
#include "r_utils/r_exception.h"
#include <filesystem>

using namespace r_vss;
using namespace r_disco;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_record_from);

namespace
{

// A camera with the id "cam-1" in a fresh devices directory.
struct fixture
{
    string dir;
    unique_ptr<r_devices> devices;

    fixture(const char* name)
    {
        dir = (filesystem::temp_directory_path() / name).string();
        filesystem::remove_all(dir);
        filesystem::create_directories(dir);
        devices = make_unique<r_devices>(dir);
        devices->start();
        r_stream_config sc;
        sc.id = "cam-1";
        sc.ipv4 = "10.0.0.5";
        sc.rtsp_url = "rtsp://10.0.0.5/stream1";
        devices->insert_or_update_devices({{sc, hash_stream_config(sc)}});
    }
    ~fixture()
    {
        devices->stop();
        devices.reset();
        filesystem::remove_all(dir);
    }

    r_nullable<string> record_from() { return devices->get_camera_by_id("cam-1").value().record_from; }

    // The camera's substream as found: on or off, with its status.
    void substream(const char* mode, const char* status)
    {
        auto c = devices->get_camera_by_id("cam-1").value();
        c.sub_mode.set_value(mode);
        c.sub_status.set_value(status);
        devices->save_camera(c);
    }

    void update(const r_nullable<string>& record_from)
    {
        query_update_camera_properties(dir, *devices, "cam-1", false, false, 24, r_nullable<string>(), r_nullable<string>(), r_nullable<string>(), record_from);
    }

    bool refused(const string& value)
    {
        try { update(value); }
        catch(const r_http::r_http_400_exception&) { return true; }
        return false;
    }
};

}

// /update_camera_properties' "record_from": set when given, left alone when not, refused when it isn't one.
void test_r_record_from::test_update_properties_sets_record_from()
{
    fixture f("r_record_from_test_set");
    // Existing cameras (null) record from main.
    RTF_ASSERT(f.record_from().is_null());
    RTF_ASSERT(!record_from_sub(f.devices->get_camera_by_id("cam-1").value()));

    f.substream("on", "ok");
    f.update(string("sub"));
    RTF_ASSERT(f.record_from().value() == "sub");
    RTF_ASSERT(record_from_sub(f.devices->get_camera_by_id("cam-1").value()));

    // Not given: kept.
    query_update_camera_properties(f.dir, *f.devices, "cam-1", false, false, 24, string("2"));
    RTF_ASSERT(f.record_from().value() == "sub");

    f.update(string("main"));
    RTF_ASSERT(f.record_from().value() == "main");
    RTF_ASSERT(!record_from_sub(f.devices->get_camera_by_id("cam-1").value()));

    for(auto bad : {"", "Sub", "substream", "hd"})
        RTF_ASSERT(f.refused(bad));
    RTF_ASSERT(f.record_from().value() == "main");    // a refused change changes nothing

    bool not_found = false;
    try { query_update_camera_properties(f.dir, *f.devices, "no-such", false, false, 24, r_nullable<string>(), r_nullable<string>(), r_nullable<string>(), string("main")); }
    catch(const r_not_found_exception&) { not_found = true; }
    RTF_ASSERT(not_found);
}

// The substream is chosen only when it works, but a choice already made is kept while it doesn't (a client that
// saves the other settings again sends it back), and main can always be chosen.
void test_r_record_from::test_the_substream_is_only_chosen_while_it_works()
{
    fixture f("r_record_from_test_usable");

    // No substream chosen for the camera, one that isn't found, or one not checked yet: refused.
    RTF_ASSERT(f.refused("sub"));
    f.substream("off", "ok");
    RTF_ASSERT(f.refused("sub"));
    f.substream("on", "none:no smaller stream");
    RTF_ASSERT(f.refused("sub"));
    f.substream("on", "pending");
    RTF_ASSERT(f.refused("sub"));
    RTF_ASSERT(f.record_from().is_null());

    f.substream("on", "ok");
    f.update(string("sub"));
    RTF_ASSERT(f.record_from().value() == "sub");

    // The substream goes away: the choice stays, and sending it again is not refused.
    f.substream("on", "none:camera allows one stream at a time");
    f.update(string("sub"));
    RTF_ASSERT(f.record_from().value() == "sub");
    // Main can always be chosen, and then sub can't be again until it works.
    f.update(string("main"));
    RTF_ASSERT(f.record_from().value() == "main");
    RTF_ASSERT(f.refused("sub"));
}
