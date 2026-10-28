
#include "test_r_disco.h"
#include "r_disco/r_agent.h"
#include "r_disco/r_devices.h"
#include "r_disco/r_camera_move.h"
#include <chrono>
#include <map>
#include "r_utils/r_file.h"

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
