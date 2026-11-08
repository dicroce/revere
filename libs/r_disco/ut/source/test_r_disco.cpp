
#include "test_r_disco.h"
#include "r_disco/r_agent.h"
#include "r_disco/r_devices.h"
#include "r_disco/r_camera_move.h"
#include <chrono>
#include <map>
#include "r_utils/r_file.h"
#include "r_db/r_sqlite_conn.h"
#include "r_utils/r_logger.h"
#include <set>
#include <mutex>

using namespace std;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_disco);

using namespace r_disco;

void test_r_disco::setup()
{
    r_fs::mkdir("top_dir");
}

void test_r_disco::teardown()
{
    // The cameras.db tests leave a database behind.
    r_logger::clear_log_callback();
    // The r_devices tests leave a database behind.
    auto db_dir = string("top_dir") + PATH_SLASH + "db";
    for(auto f : {"cameras.db", "cameras.db-wal", "cameras.db-shm", "cameras.db-journal"})
    {
        auto path = db_dir + PATH_SLASH + f;
        if(r_fs::file_exists(path))
            r_fs::remove_file(path);
    }
    if(r_fs::file_exists(db_dir))
        r_fs::rmdir(db_dir);
    r_fs::rmdir("top_dir");
}

void test_r_disco::test_r_disco_r_agent_basics()
{
    r_agent agent("top_dir");
    vector<pair<r_stream_config, string>> saved_stream_configs;

    agent.set_credential_cb([](const string& id){
        printf("credential_cb: %s\n", id.c_str());
        return make_pair(r_nullable<string>("root"), r_nullable<string>("emperor1"));
    });
    agent.set_stream_change_cb([&](const vector<pair<r_stream_config, string>>& stream_configs){
        saved_stream_configs = stream_configs;
    });
    agent.set_is_recording_cb([](const string& id){
        printf("is_recording_cb: %s\n", id.c_str());
        return false;
    });
    agent.start();
    this_thread::sleep_for(chrono::milliseconds(20000));
    agent.stop();

    for (const auto& stream_config : saved_stream_configs) {
        printf("stream_config: %s\n", stream_config.first.camera_name.value().c_str());
        printf("stream_config: %s\n", stream_config.first.ipv4.value().c_str());
        printf("stream_config: %s\n", stream_config.first.xaddrs.value().c_str());
        printf("stream_config: %s\n", stream_config.first.address.value().c_str());
        printf("stream_config: %s\n", stream_config.first.rtsp_url.value().c_str());
    }
}

// ---------------------------------------------------------------------------
// An assigned camera found at a new address (r_camera_move.h): followed only
// when its old address has gone silent for MOVE_FOLLOW_AFTER and the new one
// has the camera's stored MAC; otherwise held, and nothing is written.

namespace
{
r_camera _assigned(const string& id, const string& ipv4, const r_nullable<string>& mac)
{
    r_camera c;
    c.id = id;
    c.state = "assigned";
    c.ipv4.set_value(ipv4);
    c.onvif_port.set_value(80);
    c.xaddrs.set_value("/onvif/device_service");
    c.rtsp_url.set_value("rtsp://" + ipv4 + ":554/stream1");
    if(!mac.is_null())
        c.mac = mac;
    return c;
}

r_stream_config _discovered(const string& id, const string& ipv4)
{
    r_stream_config sc;
    sc.id = id;
    sc.ipv4.set_value(ipv4);
    sc.port.set_value(80);
    sc.xaddrs.set_value("/onvif/device_service");
    return sc;
}

r_nullable<string> _s(const string& v) { r_nullable<string> n; n.set_value(v); return n; }

// An agent wired to fakes: one stored camera, writes recorded, moves recorded,
// the old address alive or not, a MAC for the new address, and a clock.
struct fake_host
{
    map<string, r_camera> cameras;
    vector<r_stream_config> written;
    map<string, r_nullable<r_camera_move>> moves;
    vector<r_camera> saved;
    bool old_alive {true};
    r_nullable<string> new_mac;
    int64_t now_ms {1'800'000'000'000LL};

    void wire(r_agent& agent)
    {
        agent.set_camera_lookup_cb([this](const string& id){
            r_nullable<r_camera> c;
            auto it = cameras.find(id);
            if(it != cameras.end()) c.set_value(it->second);
            return c;
        });
        agent.set_stream_change_cb([this](const vector<pair<r_stream_config, string>>& scs){
            for(auto& p : scs) written.push_back(p.first);
        });
        agent.set_camera_move_cb([this](const string& id, const r_nullable<r_camera_move>& m){ moves[id] = m; });
        agent.set_save_camera_cb([this](const r_camera& c){ saved.push_back(c); cameras[c.id] = c; });
        agent.set_address_alive_probe([this](const r_camera&){ return old_alive; });
        agent.set_mac_lookup([this](const string&, int){ return new_mac; });
        agent.set_clock_ms([this](){ return now_ms; });
    }
    void minutes(int m) { now_ms += (int64_t)m * 60 * 1000; }
};
}

void test_r_disco::test_camera_move_from()
{
    auto stored = _assigned("cam1", "10.0.0.5", _s("00:1b:2c:3d:4e:5f"));
    RTF_ASSERT(camera_move_from(stored, _discovered("cam1", "10.0.0.5")).is_null());

    auto moved = camera_move_from(stored, _discovered("cam1", "10.0.0.9"));
    RTF_ASSERT(!moved.is_null());
    RTF_ASSERT(moved.value().old_ipv4 == "10.0.0.5" && moved.value().new_ipv4 == "10.0.0.9");
    RTF_ASSERT(moved.value().status == "held");

    auto port = _discovered("cam1", "10.0.0.5");
    port.port.set_value(8000);
    RTF_ASSERT(!camera_move_from(stored, port).is_null());

    auto path = _discovered("cam1", "10.0.0.5");
    path.xaddrs.set_value("/elsewhere");
    RTF_ASSERT(!camera_move_from(stored, path).is_null());

    // Only assigned cameras hold credentials worth guarding.
    auto discovered_only = stored;
    discovered_only.state = "discovered";
    RTF_ASSERT(camera_move_from(discovered_only, _discovered("cam1", "10.0.0.9")).is_null());

    // A field discovery didn't report counts as unchanged.
    r_stream_config no_ip;
    no_ip.id = "cam1";
    RTF_ASSERT(camera_move_from(stored, no_ip).is_null());
}

void test_r_disco::test_decide_camera_move()
{
    using namespace std::chrono;
    auto mac = _s("00:1b:2c:3d:4e:5f");
    auto five = duration_cast<milliseconds>(MOVE_FOLLOW_AFTER);

    // The old address still answers: not a move.
    RTF_ASSERT(decide_camera_move(true, five, mac, mac) == move_outcome::hold);
    // The new address's MAC isn't the camera's.
    RTF_ASSERT(decide_camera_move(false, five, mac, _s("aa:aa:aa:aa:aa:aa")) == move_outcome::hold);
    // The old address silent long enough, and the MAC matches (in any spelling): followed.
    RTF_ASSERT(decide_camera_move(false, five, mac, mac) == move_outcome::follow);
    RTF_ASSERT(decide_camera_move(false, five, mac, _s("00-1B-2C-3D-4E-5F")) == move_outcome::follow);
    // No stored MAC, or none read for the new address.
    RTF_ASSERT(decide_camera_move(false, five, r_nullable<string>(), mac) == move_outcome::hold);
    RTF_ASSERT(decide_camera_move(false, five, mac, r_nullable<string>()) == move_outcome::hold);
    // Silent, but not for long enough yet.
    RTF_ASSERT(decide_camera_move(false, five - milliseconds(1), mac, mac) == move_outcome::hold);
}

void test_r_disco::test_agent_holds_a_move_and_writes_nothing()
{
    r_agent agent("top_dir");
    fake_host host;
    host.cameras["cam1"] = _assigned("cam1", "10.0.0.5", _s("00:1b:2c:3d:4e:5f"));
    host.wire(agent);

    agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.9"), "h1"}});
    RTF_ASSERT(host.written.empty());
    RTF_ASSERT(!host.moves["cam1"].is_null());
    RTF_ASSERT(host.moves["cam1"].value().status == "held");
    RTF_ASSERT(agent.camera_moves().count("cam1") == 1);

    // Another camera that didn't move is written as before.
    host.cameras["cam2"] = _assigned("cam2", "10.0.0.6", _s("00:00:00:00:00:02"));
    agent.apply_discovered_changes({{_discovered("cam2", "10.0.0.6"), "h2"}});
    RTF_ASSERT(host.written.size() == 1 && host.written[0].id == "cam2");

    // Seen back at its stored address: the held move is dropped.
    agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.5"), "h3"}});
    RTF_ASSERT(host.moves["cam1"].is_null());
    RTF_ASSERT(agent.camera_moves().count("cam1") == 0);
}

void test_r_disco::test_agent_follows_when_old_silent_and_mac_matches()
{
    r_agent agent("top_dir");
    fake_host host;
    host.cameras["cam1"] = _assigned("cam1", "10.0.0.5", _s("00:1b:2c:3d:4e:5f"));
    host.wire(agent);
    host.new_mac = _s("00:1B:2C:3D:4E:5F");

    agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.9"), "h1"}});
    host.old_alive = false;
    agent.recheck_held_moves();          // silent from now
    host.minutes(4);
    agent.recheck_held_moves();          // 4 minutes: not yet
    RTF_ASSERT(host.written.empty());
    host.minutes(1);
    agent.recheck_held_moves();          // 5 minutes: followed
    RTF_ASSERT(host.written.size() == 1 && host.written[0].ipv4.value() == "10.0.0.9");
    RTF_ASSERT(host.moves["cam1"].value().status == "followed");
    RTF_ASSERT(agent.camera_moves().at("cam1").status == "followed");

    // The followed notice stays a day, then goes.
    host.minutes(24 * 60 + 1);
    agent.recheck_held_moves();
    RTF_ASSERT(host.moves["cam1"].is_null());
}

void test_r_disco::test_agent_holds_when_old_alive_mac_differs_or_unknown()
{
    // The old address answers again partway: the silence starts over.
    {
        r_agent agent("top_dir");
        fake_host host;
        host.cameras["cam1"] = _assigned("cam1", "10.0.0.5", _s("00:1b:2c:3d:4e:5f"));
        host.wire(agent);
        host.new_mac = _s("00:1b:2c:3d:4e:5f");
        agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.9"), "h1"}});
        host.old_alive = false;
        agent.recheck_held_moves();
        host.minutes(3);
        host.old_alive = true;
        agent.recheck_held_moves();
        host.old_alive = false;
        host.minutes(3);
        agent.recheck_held_moves();
        host.minutes(3);
        agent.recheck_held_moves();
        RTF_ASSERT(host.written.empty());
        RTF_ASSERT(host.moves["cam1"].value().status == "held");
    }
    // A different MAC at the new address.
    {
        r_agent agent("top_dir");
        fake_host host;
        host.cameras["cam1"] = _assigned("cam1", "10.0.0.5", _s("00:1b:2c:3d:4e:5f"));
        host.wire(agent);
        host.new_mac = _s("66:77:88:99:aa:bb");
        agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.9"), "h1"}});
        host.old_alive = false;
        agent.recheck_held_moves();
        host.minutes(10);
        agent.recheck_held_moves();
        RTF_ASSERT(host.written.empty());
        RTF_ASSERT(host.moves["cam1"].value().status == "held");
    }
    // No MAC stored for the camera.
    {
        r_agent agent("top_dir");
        fake_host host;
        host.cameras["cam1"] = _assigned("cam1", "10.0.0.5", r_nullable<string>());
        host.wire(agent);
        host.new_mac = _s("00:1b:2c:3d:4e:5f");
        agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.9"), "h1"}});
        host.old_alive = false;
        agent.recheck_held_moves();
        host.minutes(10);
        agent.recheck_held_moves();
        RTF_ASSERT(host.written.empty());
        RTF_ASSERT(host.moves["cam1"].value().status == "held");
    }
}

void test_r_disco::test_agent_confirm_and_dismiss()
{
    r_agent agent("top_dir");
    fake_host host;
    host.cameras["cam1"] = _assigned("cam1", "10.0.0.5", r_nullable<string>());
    host.wire(agent);

    // Keep current: nothing written, and the same new address isn't raised again...
    agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.9"), "h1"}});
    RTF_ASSERT(agent.dismiss_camera_move("cam1"));
    RTF_ASSERT(host.moves["cam1"].is_null());
    agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.9"), "h2"}});
    RTF_ASSERT(host.moves["cam1"].is_null());
    RTF_ASSERT(host.written.empty());
    // ...but a different one is.
    agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.10"), "h3"}});
    RTF_ASSERT(!host.moves["cam1"].is_null());

    // Use new address: written at once, and noted as followed.
    RTF_ASSERT(agent.confirm_camera_move("cam1"));
    RTF_ASSERT(host.written.size() == 1 && host.written[0].ipv4.value() == "10.0.0.10");
    RTF_ASSERT(host.moves["cam1"].value().status == "followed");
    agent.clear_followed_move("cam1");
    RTF_ASSERT(host.moves["cam1"].is_null());

    RTF_ASSERT(!agent.confirm_camera_move("nope"));
    RTF_ASSERT(!agent.dismiss_camera_move("nope"));
}

void test_r_disco::test_agent_backfills_mac_at_stored_address()
{
    r_agent agent("top_dir");
    fake_host host;
    host.cameras["cam1"] = _assigned("cam1", "10.0.0.5", r_nullable<string>());
    host.wire(agent);
    host.new_mac = _s("00:1b:2c:3d:4e:5f");

    agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.5"), "h1"}});
    RTF_ASSERT(host.saved.size() == 1);
    RTF_ASSERT(host.saved[0].mac.value() == "00:1b:2c:3d:4e:5f");

    // Already known: not looked up or saved again.
    agent.apply_discovered_changes({{_discovered("cam1", "10.0.0.5"), "h2"}});
    RTF_ASSERT(host.saved.size() == 1);
}

static string cameras_db_path()
{
    return string("top_dir") + PATH_SLASH + "db" + PATH_SLASH + "cameras.db";
}

// cameras.db as an older build left it: the original table, the columns of
// versions 2 to 7, then the given extra columns, at the given user_version.
static void make_cameras_db(int version, const vector<string>& extra_columns, const string& drop = string())
{
    r_fs::mkdir(string("top_dir") + PATH_SLASH + "db");
    r_db::r_sqlite_conn conn(cameras_db_path(), true);
    conn.exec(
        "CREATE TABLE cameras ("
            "id TEXT PRIMARY KEY NOT NULL UNIQUE, camera_name TEXT, friendly_name TEXT, ipv4 TEXT, "
            "xaddrs TEXT, address TEXT, rtsp_url TEXT, rtsp_username TEXT, rtsp_password TEXT, "
            "video_codec TEXT, video_codec_parameters TEXT, video_timebase INTEGER, "
            "audio_codec TEXT, audio_codec_parameters TEXT, audio_timebase INTEGER, state TEXT, "
            "record_file_path TEXT, n_record_file_blocks INTEGER, record_file_block_size INTEGER, "
            "do_motion_detection INTEGER, motion_detection_file_path TEXT, "
            "last_update_time INTEGER, stream_config_hash TEXT);"
    );
    vector<string> columns = {
        "do_motion_pruning INTEGER DEFAULT 0", "min_continuous_recording_hours INTEGER DEFAULT 24",
        "onvif_profile_token TEXT", "onvif_port INTEGER", "onvif_protocol TEXT", "motion_tier TEXT",
        "motion_tier_verified INTEGER", "rtsp_transport TEXT"
    };
    columns.insert(columns.end(), extra_columns.begin(), extra_columns.end());
    for(auto& c : columns)
        if(drop.empty() || c.rfind(drop + " ", 0) != 0)
            conn.exec("ALTER TABLE cameras ADD COLUMN " + c + ";");
    conn.exec("INSERT INTO cameras (id, camera_name, ipv4, state, rtsp_url) VALUES ('cam-1', 'Cats', '10.0.0.5', 'assigned', 'rtsp://10.0.0.5/stream1');");
    conn.exec("PRAGMA user_version=" + to_string(version) + ";");
}

static int db_version()
{
    r_db::r_sqlite_conn conn(cameras_db_path(), false);
    return stoi(conn.exec("PRAGMA user_version;").front().begin()->second.value());
}

static set<string> db_columns()
{
    r_db::r_sqlite_conn conn(cameras_db_path(), false);
    set<string> names;
    for(auto& row : conn.exec("PRAGMA table_info(cameras);"))
        names.insert(row.at("name").value());
    return names;
}

// Starts r_devices on top_dir, then saves the camera's ONVIF device information,
// which names the manufacturer, model and firmware_version columns.
static r_nullable<r_camera> start_and_save_device_info()
{
    r_devices devices("top_dir");
    devices.start();
    r_stream_config sc;
    sc.id = "cam-1";
    sc.ipv4 = "10.0.0.5";
    sc.rtsp_url = "rtsp://10.0.0.5/stream1";
    sc.manufacturer = "TP-Link";
    sc.model = "C500";
    sc.firmware_version = "1.3.7";
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
    auto camera = devices.get_camera_by_id("cam-1");
    devices.stop();
    return camera;
}

void test_r_disco::test_db_upgrade_from_mac_at_version_8()
{
    // One build added mac as version 8; another added the device information as
    // version 8 and mac as 9. A database from the first is at 8 with mac and
    // without the device information. It used to fail on "duplicate column name:
    // mac" at every start, and every write naming the device information failed.
    make_cameras_db(8, {"mac TEXT"});
    {
        r_db::r_sqlite_conn conn(cameras_db_path(), true);
        conn.exec("UPDATE cameras SET mac='aa:bb:cc:dd:ee:ff' WHERE id='cam-1';");
    }

    auto camera = start_and_save_device_info();
    RTF_ASSERT(!camera.is_null());
    RTF_ASSERT(camera.value().manufacturer.value() == "TP-Link");
    RTF_ASSERT(camera.value().firmware_version.value() == "1.3.7");
    RTF_ASSERT(camera.value().mac.value() == "aa:bb:cc:dd:ee:ff");

    RTF_ASSERT_EQUAL(db_version(), 11);
    auto columns = db_columns();
    for(auto c : {"manufacturer", "model", "firmware_version", "mac"})
        RTF_ASSERT(columns.count(c) == 1);
}

void test_r_disco::test_db_upgrade_from_device_info_at_version_8()
{
    // The other order: the device information at 8, without mac.
    make_cameras_db(8, {"manufacturer TEXT", "model TEXT", "firmware_version TEXT"});
    auto camera = start_and_save_device_info();
    RTF_ASSERT(camera.value().model.value() == "C500");
    RTF_ASSERT_EQUAL(db_version(), 11);
    RTF_ASSERT(db_columns().count("mac") == 1);
}

void test_r_disco::test_db_upgrade_repairs_a_missing_column()
{
    // At the latest version but without a column: it's added anyway.
    make_cameras_db(9, {"manufacturer TEXT", "model TEXT", "firmware_version TEXT", "mac TEXT"}, "model");
    RTF_ASSERT(db_columns().count("model") == 0);
    auto camera = start_and_save_device_info();
    RTF_ASSERT(camera.value().model.value() == "C500");
    RTF_ASSERT(db_columns().count("model") == 1);
    RTF_ASSERT_EQUAL(db_version(), 11);
}

void test_r_disco::test_db_upgrade_keeps_a_newer_version()
{
    // A database a newer build has used keeps its version.
    make_cameras_db(12, {"manufacturer TEXT", "model TEXT", "firmware_version TEXT", "mac TEXT"});
    auto camera = start_and_save_device_info();
    RTF_ASSERT(!camera.is_null());
    RTF_ASSERT_EQUAL(db_version(), 12);
}

void test_r_disco::test_db_upgrade_twice()
{
    // A new database, then a second start on it: nothing to add, nothing fails.
    auto first = start_and_save_device_info();
    RTF_ASSERT(first.value().manufacturer.value() == "TP-Link");
    RTF_ASSERT_EQUAL(db_version(), 11);
    auto columns = db_columns();
    auto second = start_and_save_device_info();
    RTF_ASSERT(second.value().manufacturer.value() == "TP-Link");
    RTF_ASSERT(db_columns() == columns);
    RTF_ASSERT_EQUAL(db_version(), 11);
}

void test_r_disco::test_db_upgrade_from_version_7()
{
    // A database from before either version 8: none of manufacturer, model,
    // firmware_version or mac. All four are added, and the version becomes the latest.
    make_cameras_db(7, {});
    auto before = db_columns();
    for(auto c : {"manufacturer", "model", "firmware_version", "mac"})
        RTF_ASSERT(before.count(c) == 0);

    auto camera = start_and_save_device_info();
    RTF_ASSERT(camera.value().manufacturer.value() == "TP-Link");
    RTF_ASSERT(camera.value().firmware_version.value() == "1.3.7");
    RTF_ASSERT_EQUAL(db_version(), 11);
    auto after = db_columns();
    for(auto c : {"manufacturer", "model", "firmware_version", "mac"})
        RTF_ASSERT(after.count(c) == 1);
}

// Log lines that contain `what`, from set_log_callback.
struct log_capture
{
    log_capture(const string& what) : _what(what)
    {
        r_logger::set_log_callback([this](r_logger::LOG_LEVEL, const string& msg) {
            if(msg.find(_what) != string::npos)
            {
                lock_guard<mutex> g(_lock);
                _lines.push_back(msg);
            }
        });
    }
    ~log_capture() { r_logger::clear_log_callback(); }
    vector<string> lines() { lock_guard<mutex> g(_lock); return _lines; }
    string _what;
    mutex _lock;
    vector<string> _lines;
};

void test_r_disco::test_startup_logs_the_database()
{
    // Which file it opened and its schema version, so a log shows which data
    // dir a gateway runs from.
    log_capture log("Cameras database:");
    {
        r_devices devices("top_dir");
        devices.start();
        devices.get_all_cameras();
        devices.stop();
    }
    auto lines = log.lines();
    RTF_ASSERT_EQUAL(lines.size(), (size_t)1);
    RTF_ASSERT(lines[0].find(string("top_dir") + PATH_SLASH + "db" + PATH_SLASH + "cameras.db") != string::npos);
    RTF_ASSERT(lines[0].find("(schema version 11, was 0)") != string::npos);
}

void test_r_disco::test_failed_writes_are_logged()
{
    // insert_or_update_devices doesn't wait for its write. One that fails is
    // logged, at most once a minute however often discovery tries.
    log_capture log("Saving discovered cameras failed");
    r_devices devices("top_dir");
    devices.start();
    devices.get_all_cameras();

    // Refuse inserts, so the write that fails is the INSERT itself, whose text
    // has the camera's address and URL in it.
    {
        r_db::r_sqlite_conn conn(string("top_dir") + PATH_SLASH + "db" + PATH_SLASH + "cameras.db", true);
        conn.exec("CREATE TRIGGER refuse_writes BEFORE INSERT ON cameras BEGIN SELECT RAISE(ABORT, 'writes refused'); END;");
    }
    r_stream_config sc;
    sc.id = "cam-1";
    sc.ipv4 = "10.0.0.5";
    // A URL with a user in it, as some cameras give.
    sc.rtsp_url = "rtsp://secret-user@10.0.0.5/stream1";
    for(int i = 0; i < 3; ++i)
        devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
    // A command after them, so they have run (it fails too, and logs its own error).
    devices.get_all_cameras();
    devices.stop();

    auto lines = log.lines();
    RTF_ASSERT_EQUAL(lines.size(), (size_t)1);
    // SQLite's reason when it gives one; never the query, which has the camera's
    // address and credentials in it.
    RTF_ASSERT(lines[0].find("Saving discovered cameras failed: ") != string::npos);
    RTF_ASSERT(lines[0].find("10.0.0.5") == string::npos);
    RTF_ASSERT(lines[0].find("secret-user") == string::npos);
    RTF_ASSERT(lines[0].find("INSERT") == string::npos);
}

static r_camera substream_camera(r_devices& devices)
{
    r_stream_config sc;
    sc.id = "cam-1";
    sc.ipv4 = "10.0.0.5";
    sc.rtsp_url = "rtsp://10.0.0.5/stream1";
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
    return devices.get_camera_by_id("cam-1").value();
}

void test_r_disco::test_substream_saved_and_loaded()
{
    r_camera unset;
    RTF_ASSERT(!substream_on(unset));
    unset.sub_mode = string("On");
    RTF_ASSERT(!substream_on(unset));
    unset.sub_mode = string("on");
    RTF_ASSERT(substream_on(unset));

    r_devices devices("top_dir");
    devices.start();
    auto c = substream_camera(devices);
    RTF_ASSERT(c.sub_mode.value() == "off");   // a new camera has none
    RTF_ASSERT(c.sub_rtsp_url.is_null());
    RTF_ASSERT(c.sub_analysis.is_null() || c.sub_analysis.value() == 0);     // and finds no motion in it

    c.sub_mode = string("on");
    c.sub_status = string("ok");
    c.sub_onvif_profile_token = string("Profile_2");
    c.sub_rtsp_url = string("rtsp://10.0.0.5/stream2?it's=bound");  // a quote: bound, not formatted into the SQL
    c.sub_video_codec = string("h264");
    c.sub_video_codec_parameters = string("sprop-parameter-sets=Z2QAKA==,aO48gA==");
    c.sub_video_timebase = 90000;
    c.sub_width = 640;
    c.sub_height = 480;
    c.sub_key_interval_ms = 1000;
    c.sub_analysis = 1;
    devices.save_camera(c);

    auto r = devices.get_camera_by_id("cam-1").value();
    RTF_ASSERT(r.sub_mode.value() == "on");
    RTF_ASSERT(r.sub_status.value() == "ok");
    RTF_ASSERT(r.sub_onvif_profile_token.value() == "Profile_2");
    RTF_ASSERT(r.sub_rtsp_url.value() == "rtsp://10.0.0.5/stream2?it's=bound");
    RTF_ASSERT(r.sub_video_codec.value() == "h264");
    RTF_ASSERT(r.sub_video_codec_parameters.value() == "sprop-parameter-sets=Z2QAKA==,aO48gA==");
    RTF_ASSERT_EQUAL(r.sub_video_timebase.value(), 90000);
    RTF_ASSERT_EQUAL(r.sub_width.value(), 640);
    RTF_ASSERT_EQUAL(r.sub_height.value(), 480);
    RTF_ASSERT_EQUAL(r.sub_key_interval_ms.value(), 1000);
    RTF_ASSERT_EQUAL(r.sub_analysis.value(), 1);

    // Rediscovery updates the camera's own fields and keeps its substream.
    r_stream_config sc;
    sc.id = "cam-1";
    sc.ipv4 = "10.0.0.6";
    sc.rtsp_url = "rtsp://10.0.0.6/stream1";
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
    auto moved = devices.get_camera_by_id("cam-1").value();
    RTF_ASSERT(moved.ipv4.value() == "10.0.0.6");
    RTF_ASSERT(moved.sub_rtsp_url.value() == "rtsp://10.0.0.5/stream2?it's=bound");
    RTF_ASSERT_EQUAL(moved.sub_width.value(), 640);
    RTF_ASSERT_EQUAL(moved.sub_analysis.value(), 1);

    // Turned off: null clears every field, and anything but "on" is stored as off.
    r_camera off = moved;
    off.sub_mode = string("maybe");
    off.sub_status.clear();
    off.sub_onvif_profile_token.clear();
    off.sub_rtsp_url.clear();
    off.sub_video_codec.clear();
    off.sub_video_codec_parameters.clear();
    off.sub_video_timebase.clear();
    off.sub_width.clear();
    off.sub_height.clear();
    off.sub_key_interval_ms.clear();
    devices.save_camera(off);
    auto o = devices.get_camera_by_id("cam-1").value();
    RTF_ASSERT(o.sub_mode.value() == "off");
    RTF_ASSERT(o.sub_status.is_null());
    RTF_ASSERT(o.sub_onvif_profile_token.is_null());
    RTF_ASSERT(o.sub_rtsp_url.is_null());
    RTF_ASSERT(o.sub_video_codec.is_null());
    RTF_ASSERT(o.sub_video_codec_parameters.is_null());
    RTF_ASSERT(o.sub_video_timebase.is_null());
    RTF_ASSERT(o.sub_width.is_null());
    RTF_ASSERT(o.sub_height.is_null());
    RTF_ASSERT(o.sub_key_interval_ms.is_null());

    devices.stop();
}

void test_r_disco::test_db_upgrade_existing_camera_has_no_substream()
{
    // A camera from before the substream columns reads as off after the upgrade,
    // so an upgrade opens no second stream.
    make_cameras_db(9, {"manufacturer TEXT", "model TEXT", "firmware_version TEXT", "mac TEXT"});
    r_devices devices("top_dir");
    devices.start();
    auto c = devices.get_camera_by_id("cam-1").value();
    RTF_ASSERT(c.sub_mode.value() == "off");
    RTF_ASSERT(c.sub_status.is_null());
    RTF_ASSERT(c.sub_rtsp_url.is_null());
    RTF_ASSERT(c.sub_analysis.is_null() || c.sub_analysis.value() == 0);   // an existing camera finds motion where it did
    devices.stop();
    RTF_ASSERT_EQUAL(db_version(), 11);
    auto columns = db_columns();
    for(auto col : {"sub_mode", "sub_status", "sub_onvif_profile_token", "sub_rtsp_url", "sub_video_codec",
                    "sub_video_codec_parameters", "sub_video_timebase", "sub_width", "sub_height", "sub_key_interval_ms", "sub_analysis"})
        RTF_ASSERT(columns.count(col) == 1);
}
