
#include "test_r_rtsp_transport.h"
#include "r_vss/r_rtsp_transport.h"
#include "r_disco/r_devices.h"
#include "r_db/r_sqlite_conn.h"
#include "r_utils/r_file.h"
#include <filesystem>

using namespace std;
using namespace r_vss;

REGISTER_TEST_FIXTURE(test_r_rtsp_transport);

using namespace r_utils;

void test_r_rtsp_transport::setup()
{
    // The cameras.db tests start from an empty top_dir
    std::filesystem::remove_all("top_dir");
    r_fs::mkdir("top_dir");
}

void test_r_rtsp_transport::teardown()
{
    std::filesystem::remove_all("top_dir");
}

void test_r_rtsp_transport::test_starts_on_tcp()
{
    r_rtsp_transport t;
    RTF_ASSERT(t.protocols() == "TCP");
    RTF_ASSERT(!t.confirmed());
}

void test_r_rtsp_transport::test_unreachable_camera_stays_on_tcp()
{
    // A camera that's off at startup never answers. That says nothing about
    // transports, so it must not end up on UDP when it comes back.
    r_rtsp_transport t;
    for(int i = 0; i < 10; ++i)
        RTF_ASSERT(!t.attempt_ended(false, false));
    RTF_ASSERT(t.protocols() == "TCP");
}

void test_r_rtsp_transport::test_tcp_refused_falls_back_to_udp()
{
    // The camera answers (DESCRIBE works) but sends no video over TCP
    r_rtsp_transport t;
    RTF_ASSERT(t.attempt_ended(true, false));
    RTF_ASSERT(t.protocols() == "UDP");

    // UDP works, and from then on the camera stays on it
    RTF_ASSERT(!t.attempt_ended(true, true));
    RTF_ASSERT(t.confirmed());
    RTF_ASSERT(!t.attempt_ended(true, false));
    RTF_ASSERT(!t.attempt_ended(false, false));
    RTF_ASSERT(t.protocols() == "UDP");
}

void test_r_rtsp_transport::test_working_transport_is_kept()
{
    // Once TCP has delivered video, a later attempt that answers but stalls
    // (a camera rebooting mid-setup, say) doesn't move it to UDP
    r_rtsp_transport t;
    RTF_ASSERT(!t.attempt_ended(true, true));
    RTF_ASSERT(!t.attempt_ended(true, false));
    RTF_ASSERT(!t.attempt_ended(true, false));
    RTF_ASSERT(t.protocols() == "TCP");
}

void test_r_rtsp_transport::test_neither_working_alternates()
{
    // Until one transport has worked, answered-but-no-video keeps trying the
    // other, so a camera that briefly failed over TCP gets TCP again
    r_rtsp_transport t;
    RTF_ASSERT(t.attempt_ended(true, false));
    RTF_ASSERT(t.protocols() == "UDP");
    RTF_ASSERT(t.attempt_ended(true, false));
    RTF_ASSERT(t.protocols() == "TCP");
    RTF_ASSERT(!t.attempt_ended(true, true));
    RTF_ASSERT(t.protocols() == "TCP");
}

void test_r_rtsp_transport::test_rtsp_transport_saved()
{
    r_disco::r_devices devices("top_dir");
    devices.start();

    r_disco::r_camera tcp;
    tcp.id = "cam-default";
    tcp.state = "assigned";
    devices.save_camera(tcp);

    r_disco::r_camera udp;
    udp.id = "cam-udp";
    udp.state = "assigned";
    udp.rtsp_transport.set_value("UDP");
    devices.save_camera(udp);

    // Not set: null, which means TCP
    auto t = devices.get_camera_by_id("cam-default");
    RTF_ASSERT(!t.is_null());
    RTF_ASSERT(t.value().rtsp_transport.is_null());

    auto u = devices.get_camera_by_id("cam-udp");
    RTF_ASSERT(!u.is_null());
    RTF_ASSERT(!u.value().rtsp_transport.is_null() && u.value().rtsp_transport.value() == "UDP");

    // Back to TCP
    auto c = u.value();
    c.rtsp_transport.set_value("TCP");
    devices.save_camera(c);
    u = devices.get_camera_by_id("cam-udp");
    RTF_ASSERT(u.value().rtsp_transport.value() == "TCP");

    // Anything but TCP or UDP is stored as TCP, and never reaches the SQL as
    // is. From UDP, so a save that failed instead would leave UDP behind.
    c.rtsp_transport.set_value("UDP");
    devices.save_camera(c);
    RTF_ASSERT(devices.get_camera_by_id("cam-udp").value().rtsp_transport.value() == "UDP");
    c.rtsp_transport.set_value("UDP'); DROP TABLE cameras; --");
    devices.save_camera(c);
    u = devices.get_camera_by_id("cam-udp");
    RTF_ASSERT(!u.is_null());
    RTF_ASSERT(u.value().rtsp_transport.value() == "TCP");
    RTF_ASSERT(!devices.get_camera_by_id("cam-default").is_null());

    devices.stop();
}

void test_r_rtsp_transport::test_rtsp_transport_upgrade_keeps_cameras()
{
    // A cameras.db from before the rtsp_transport column (version 6) with a
    // camera in it: the upgrade adds the column, null, and keeps the row.
    r_fs::mkdir(string("top_dir") + PATH_SLASH + "db");
    {
        r_db::r_sqlite_conn conn(string("top_dir") + PATH_SLASH + "db" + PATH_SLASH + "cameras.db");
        conn.exec(
            "CREATE TABLE cameras ("
                "id TEXT PRIMARY KEY NOT NULL UNIQUE, camera_name TEXT, friendly_name TEXT, ipv4 TEXT, xaddrs TEXT, "
                "address TEXT, rtsp_url TEXT, rtsp_username TEXT, rtsp_password TEXT, video_codec TEXT, "
                "video_codec_parameters TEXT, video_timebase INTEGER, audio_codec TEXT, audio_codec_parameters TEXT, "
                "audio_timebase INTEGER, state TEXT, record_file_path TEXT, n_record_file_blocks INTEGER, "
                "record_file_block_size INTEGER, do_motion_detection INTEGER, motion_detection_file_path TEXT, "
                "last_update_time INTEGER, stream_config_hash TEXT, do_motion_pruning INTEGER DEFAULT 0, "
                "min_continuous_recording_hours INTEGER DEFAULT 24, onvif_profile_token TEXT, onvif_port INTEGER, "
                "onvif_protocol TEXT, motion_tier TEXT, motion_tier_verified INTEGER);"
        );
        conn.exec("INSERT INTO cameras (id, friendly_name, rtsp_url, state) VALUES ('cam-old', 'Porch', 'rtsp://10.0.0.5/s0', 'assigned');");
        conn.exec("PRAGMA user_version=6;");
    }

    r_disco::r_devices devices("top_dir");
    devices.start();

    auto c = devices.get_camera_by_id("cam-old");
    RTF_ASSERT(!c.is_null());
    RTF_ASSERT(c.value().friendly_name.value() == "Porch");
    RTF_ASSERT(c.value().rtsp_url.value() == "rtsp://10.0.0.5/s0");
    RTF_ASSERT(c.value().rtsp_transport.is_null());

    auto cam = c.value();
    cam.rtsp_transport.set_value("UDP");
    devices.save_camera(cam);
    RTF_ASSERT(devices.get_camera_by_id("cam-old").value().rtsp_transport.value() == "UDP");

    devices.stop();
}
