#include "test_r_disco.h"
#include "r_disco/r_agent.h"
#include "r_disco/r_devices.h"
#include "r_utils/r_secure_store.h"
#include "r_disco/r_camera_move.h"
#include <chrono>
#include <map>
#include "r_utils/r_file.h"
#include "r_db/r_sqlite_conn.h"
#include "r_utils/r_logger.h"
#include <set>
#include <mutex>

#include "r_utils/r_std_utils.h"

using namespace std;
using namespace r_utils;

REGISTER_TEST_FIXTURE(test_r_disco);

static const char* TEST_KEY_DIR = "top_dir_keys";

using namespace r_disco;

void test_r_disco::setup()
{
    r_fs::mkdir("top_dir");
}

void test_r_disco::teardown()
{
    // test_sql_in_a_camera_id's encryption key.
    r_secure_store::set_key_dir(string());
    auto key = string(TEST_KEY_DIR) + PATH_SLASH + "encryption.key";
    if(r_fs::file_exists(key))
        r_fs::remove_file(key);
    if(r_fs::file_exists(TEST_KEY_DIR))
        r_fs::rmdir(TEST_KEY_DIR);
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

// A live test against real cameras: it needs REVERE_TEST_CAMERA_USERNAME and REVERE_TEST_CAMERA_PASSWORD
// (never put a camera's credentials in this file) and is skipped without them.
void test_r_disco::test_r_disco_r_agent_basics()
{
    auto user = r_std_utils::get_env("REVERE_TEST_CAMERA_USERNAME");
    auto pass = r_std_utils::get_env("REVERE_TEST_CAMERA_PASSWORD");
    if(user.empty() || pass.empty())
    {
        printf("skipped: set REVERE_TEST_CAMERA_USERNAME and REVERE_TEST_CAMERA_PASSWORD to run\n");
        return;
    }

    r_agent agent("top_dir");
    vector<pair<r_stream_config, string>> saved_stream_configs;

    agent.set_credential_cb([user, pass](const string& id){
        printf("credential_cb: %s\n", id.c_str());
        return make_pair(r_nullable<string>(user), r_nullable<string>(pass));
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

    RTF_ASSERT_EQUAL(db_version(), 9);
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
    RTF_ASSERT_EQUAL(db_version(), 9);
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
    RTF_ASSERT_EQUAL(db_version(), 9);
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
    RTF_ASSERT_EQUAL(db_version(), 9);
    auto columns = db_columns();
    auto second = start_and_save_device_info();
    RTF_ASSERT(second.value().manufacturer.value() == "TP-Link");
    RTF_ASSERT(db_columns() == columns);
    RTF_ASSERT_EQUAL(db_version(), 9);
}

void test_r_disco::test_db_upgrade_from_version_7()
{
    // A database from before either version 8: none of manufacturer, model,
    // firmware_version or mac. All four are added, and the version becomes 9.
    make_cameras_db(7, {});
    auto before = db_columns();
    for(auto c : {"manufacturer", "model", "firmware_version", "mac"})
        RTF_ASSERT(before.count(c) == 0);

    auto camera = start_and_save_device_info();
    RTF_ASSERT(camera.value().manufacturer.value() == "TP-Link");
    RTF_ASSERT(camera.value().firmware_version.value() == "1.3.7");
    RTF_ASSERT_EQUAL(db_version(), 9);
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
    RTF_ASSERT(lines[0].find("(schema version 9, was 0)") != string::npos);
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

// A discovered camera, as the agent writes it from a WS-Discovery reply: the
// name is whatever the camera on the network sent.
static r_stream_config discovered(const string& id, const string& name)
{
    r_stream_config sc;
    sc.id = id;
    sc.camera_name = name;
    sc.ipv4 = "10.0.0.5";
    sc.rtsp_url = "rtsp://10.0.0.5/stream1";
    return sc;
}

void test_r_disco::test_quote_in_a_discovered_name()
{
    r_devices devices("top_dir");
    devices.start();

    // Inserted, then updated: both used to build their SQL from the name.
    auto sc = discovered("cam-1", "Bob's Camera");
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
    auto camera = devices.get_camera_by_id("cam-1");
    RTF_ASSERT(!camera.is_null());
    RTF_ASSERT(camera.value().camera_name.value() == "Bob's Camera");
    RTF_ASSERT(camera.value().state == "discovered");

    sc.camera_name = "O'Brien's \"Porch\"";
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
    camera = devices.get_camera_by_id("cam-1");
    RTF_ASSERT(camera.value().camera_name.value() == "O'Brien's \"Porch\"");
    RTF_ASSERT(camera.value().stream_config_hash == hash_stream_config(sc));

    devices.stop();
}

void test_r_disco::test_sql_in_a_discovered_name()
{
    r_devices devices("top_dir");
    devices.start();

    auto other = discovered("cam-other", "Garage");
    devices.insert_or_update_devices({{other, hash_stream_config(other)}});

    // On the update path, this name used to set the camera's own state: a camera
    // on the network could assign itself.
    auto sc = discovered("cam-1", "Front");
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
    const string assign_itself = "x', state='assigned', camera_name='x";
    sc.camera_name = assign_itself;
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});
    auto camera = devices.get_camera_by_id("cam-1");
    RTF_ASSERT(camera.value().state == "discovered");
    RTF_ASSERT(camera.value().camera_name.value() == assign_itself);
    RTF_ASSERT(devices.get_assigned_cameras().empty());

    // A new camera whose name ends the statement and starts another.
    const string drop_rows = "x'); DELETE FROM cameras; --";
    auto evil = discovered("cam-evil", drop_rows);
    devices.insert_or_update_devices({{evil, hash_stream_config(evil)}});
    RTF_ASSERT(devices.get_all_cameras().size() == 3);
    RTF_ASSERT(devices.get_camera_by_id("cam-evil").value().camera_name.value() == drop_rows);
    RTF_ASSERT(devices.get_camera_by_id("cam-other").value().camera_name.value() == "Garage");

    devices.stop();
}

void test_r_disco::test_sql_in_a_camera_id()
{
    r_devices devices("top_dir");
    devices.start();

    auto sc = discovered("cam-1", "Front");
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});

    // An id that matched every row when formatted into the WHERE.
    const string any_row = "x' OR '1'='1";
    RTF_ASSERT(devices.get_camera_by_id(any_row).is_null());

    // cam-1's credentials, under a key of this test's own (never the user's).
    r_fs::mkdir_p(TEST_KEY_DIR);
    r_secure_store::set_key_dir(TEST_KEY_DIR);
    auto with_creds = devices.get_camera_by_id("cam-1").value();
    with_creds.rtsp_username = "admin";
    with_creds.rtsp_password = "s3cret";
    devices.save_camera(with_creds);
    RTF_ASSERT(devices.get_credentials("cam-1").first.value() == "admin");
    RTF_ASSERT(devices.get_credentials("cam-1").second.value() == "s3cret");

    auto creds = devices.get_credentials(any_row);
    RTF_ASSERT(creds.first.is_null());
    RTF_ASSERT(creds.second.is_null());

    r_camera stale;
    stale.id = "x\" OR \"1\"=\"1";
    stale.stream_config_hash = "none";
    RTF_ASSERT(devices.get_modified_cameras({stale}).empty());

    r_camera gone;
    gone.id = any_row;
    devices.remove_camera(gone);
    RTF_ASSERT(devices.get_all_cameras().size() == 1);

    // An id with a quote is a camera like any other.
    auto quoted = discovered("cam-'2'", "Back");
    devices.insert_or_update_devices({{quoted, hash_stream_config(quoted)}});
    RTF_ASSERT(devices.get_camera_by_id("cam-'2'").value().camera_name.value() == "Back");
    r_camera remove_quoted;
    remove_quoted.id = "cam-'2'";
    devices.remove_camera(remove_quoted);
    RTF_ASSERT(devices.get_camera_by_id("cam-'2'").is_null());
    RTF_ASSERT(devices.get_all_cameras().size() == 1);

    devices.stop();
}

void test_r_disco::test_saved_camera_round_trips()
{
    r_devices devices("top_dir");
    devices.start();

    auto sc = discovered("cam-'1'", "Front");
    devices.insert_or_update_devices({{sc, hash_stream_config(sc)}});

    auto camera = devices.get_camera_by_id("cam-'1'").value();
    camera.friendly_name = "Bob's \"Front\" Door'); DELETE FROM cameras; --";
    camera.record_file_path = "/data/it's here/cam-1.nts";
    camera.n_record_file_blocks = 2016;
    camera.record_file_block_size = 10485760;
    camera.do_motion_detection = true;
    camera.motion_tier = "a'b";
    camera.state = "assigned";
    devices.save_camera(camera);

    auto saved = devices.get_camera_by_id("cam-'1'");
    RTF_ASSERT(!saved.is_null());
    RTF_ASSERT(saved.value().friendly_name.value() == camera.friendly_name.value());
    RTF_ASSERT(saved.value().record_file_path.value() == "/data/it's here/cam-1.nts");
    RTF_ASSERT(saved.value().n_record_file_blocks.value() == 2016);
    RTF_ASSERT(saved.value().record_file_block_size.value() == 10485760);
    RTF_ASSERT(saved.value().do_motion_detection.value());
    RTF_ASSERT(saved.value().motion_tier.value() == "a'b");
    RTF_ASSERT(saved.value().camera_name.value() == "Front");
    RTF_ASSERT(saved.value().state == "assigned");
    RTF_ASSERT(devices.get_assigned_cameras().size() == 1);

    // Read back by id again for the cameras newly assigned since a snapshot.
    auto added = devices.get_assigned_cameras_added({});
    RTF_ASSERT(added.size() == 1);
    RTF_ASSERT(added.front().id == "cam-'1'");

    devices.stop();
}
