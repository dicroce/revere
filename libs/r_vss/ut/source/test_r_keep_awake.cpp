#include "test_r_keep_awake.h"
#include "r_vss/r_keep_awake.h"
#include "r_utils/r_file.h"
#include <filesystem>
#include <fstream>

using namespace r_vss;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_keep_awake);

static const int64_t SEC = 1000;

static string _top_dir()
{
    return (filesystem::temp_directory_path() / "test_r_keep_awake").string();
}

void test_r_keep_awake::setup()
{
    error_code ec;
    filesystem::remove_all(_top_dir(), ec);
    filesystem::create_directories(_top_dir(), ec);
}

void test_r_keep_awake::teardown()
{
    error_code ec;
    filesystem::remove_all(_top_dir(), ec);
}

void test_r_keep_awake::test_wanted()
{
    r_keep_awake_settings def;
    RTF_ASSERT(def.enabled);
    RTF_ASSERT(!def.on_battery);

    // On by default while plugged in, not on battery.
    RTF_ASSERT(keep_awake_wanted(def, r_power_source::ac));
    RTF_ASSERT(!keep_awake_wanted(def, r_power_source::battery));
    // A computer whose power we can't read is held, as a desktop.
    RTF_ASSERT(keep_awake_wanted(def, r_power_source::unknown));

    r_keep_awake_settings battery_too {true, true};
    RTF_ASSERT(keep_awake_wanted(battery_too, r_power_source::battery));
    RTF_ASSERT(keep_awake_wanted(battery_too, r_power_source::ac));

    // Off is off, whatever "Also on battery" says.
    r_keep_awake_settings off {false, true};
    RTF_ASSERT(!keep_awake_wanted(off, r_power_source::ac));
    RTF_ASSERT(!keep_awake_wanted(off, r_power_source::battery));
    RTF_ASSERT(!keep_awake_wanted(off, r_power_source::unknown));
}

void test_r_keep_awake::test_settings_json()
{
    auto s = parse_keep_awake_settings(keep_awake_settings_json({false, true}));
    RTF_ASSERT(!s.enabled);
    RTF_ASSERT(s.on_battery);

    // Anything unreadable gives the default for it.
    RTF_ASSERT(parse_keep_awake_settings("") == r_keep_awake_settings());
    RTF_ASSERT(parse_keep_awake_settings("not json") == r_keep_awake_settings());
    RTF_ASSERT(parse_keep_awake_settings("[true]") == r_keep_awake_settings());
    s = parse_keep_awake_settings("{\"enabled\": \"no\", \"on_battery\": true}");
    RTF_ASSERT(s.enabled);
    RTF_ASSERT(s.on_battery);
    s = parse_keep_awake_settings("{\"enabled\": false}");
    RTF_ASSERT(!s.enabled);
    RTF_ASSERT(!s.on_battery);
}

void test_r_keep_awake::test_power_from_supplies()
{
    // A desktop: nothing listed, or only mains.
    RTF_ASSERT(power_source_from_supplies({}) == r_power_source::ac);
    RTF_ASSERT(power_source_from_supplies({{"Mains", false, ""}}) == r_power_source::ac);

    // A laptop plugged in and unplugged.
    RTF_ASSERT(power_source_from_supplies({{"Mains", true, ""}, {"Battery", false, ""}}) == r_power_source::ac);
    RTF_ASSERT(power_source_from_supplies({{"Mains", false, ""}, {"Battery", false, ""}}) == r_power_source::battery);

    // Charging over USB-C: the port's supply is "USB", online.
    RTF_ASSERT(power_source_from_supplies({{"USB", true, ""}, {"Mains", false, ""}, {"Battery", false, ""}}) == r_power_source::ac);
    RTF_ASSERT(power_source_from_supplies({{"USB", false, ""}, {"Battery", false, ""}}) == r_power_source::battery);

    // A wireless mouse's battery doesn't make a desktop a laptop.
    RTF_ASSERT(power_source_from_supplies({{"Battery", false, "Device"}}) == r_power_source::ac);
    RTF_ASSERT(power_source_from_supplies({{"Mains", false, ""}, {"Battery", false, "Device"}, {"Battery", false, ""}}) == r_power_source::battery);
}

void test_r_keep_awake::test_power_from_status()
{
    RTF_ASSERT(power_source_from_status(1, 8) == r_power_source::ac);      // charging
    RTF_ASSERT(power_source_from_status(0, 1) == r_power_source::battery); // high, on battery
    RTF_ASSERT(power_source_from_status(0, 0) == r_power_source::battery);
    RTF_ASSERT(power_source_from_status(255, 128) == r_power_source::ac);  // no system battery
    RTF_ASSERT(power_source_from_status(0, 128) == r_power_source::ac);
    RTF_ASSERT(power_source_from_status(255, 255) == r_power_source::unknown);
    RTF_ASSERT(power_source_from_status(255, 1) == r_power_source::unknown);
}

void test_r_keep_awake::test_gap_without_sleep_is_paused()
{
    r_sleep_gap_detector d;
    int64_t t = 1700000000000;
    RTF_ASSERT(!d.tick(t));
    for(int i = 0; i < 10; ++i)
    {
        t += 5 * SEC;
        RTF_ASSERT(!d.tick(t));
    }
    // Late ticks under the gap aren't one.
    t += r_sleep_gap_detector::GAP_MS - 1;
    RTF_ASSERT(!d.tick(t));

    auto last = t;
    t += 90 * SEC;
    auto e = d.tick(t);
    RTF_ASSERT(e.has_value());
    RTF_ASSERT_EQUAL(e->what, string("paused"));
    RTF_ASSERT_EQUAL(e->start_ms, last);
    RTF_ASSERT_EQUAL(e->end_ms, t);

    t += 5 * SEC;
    RTF_ASSERT(!d.tick(t));
}

void test_r_keep_awake::test_gap_after_sleep_is_asleep()
{
    r_sleep_gap_detector d;
    int64_t t = 1700000000000;
    d.tick(t);
    t += 5 * SEC;
    d.tick(t);

    // The OS says it's going to sleep 2 s after a tick; it wakes an hour later.
    auto sleep_at = t + 2 * SEC;
    d.on_sleep(sleep_at);
    t = sleep_at + 3600 * SEC;
    auto e = d.tick(t);
    RTF_ASSERT(e.has_value());
    RTF_ASSERT_EQUAL(e->what, string("asleep"));
    RTF_ASSERT_EQUAL(e->start_ms, sleep_at);
    RTF_ASSERT_EQUAL(e->end_ms, t);

    // Used once: the next gap with no word from the OS is a pause.
    t += 5 * SEC;
    RTF_ASSERT(!d.tick(t));
    auto last = t;
    t += 60 * SEC;
    e = d.tick(t);
    RTF_ASSERT(e.has_value());
    RTF_ASSERT_EQUAL(e->what, string("paused"));
    RTF_ASSERT_EQUAL(e->start_ms, last);
}

void test_r_keep_awake::test_called_off_sleep_is_forgotten()
{
    r_sleep_gap_detector d;
    int64_t t = 1700000000000;
    d.tick(t);

    // Said it would sleep, then didn't: ticks carry on.
    d.on_sleep(t + 1 * SEC);
    for(int i = 0; i < 6; ++i)
    {
        t += 5 * SEC;
        RTF_ASSERT(!d.tick(t));
    }

    auto last = t;
    t += 120 * SEC;
    auto e = d.tick(t);
    RTF_ASSERT(e.has_value());
    RTF_ASSERT_EQUAL(e->what, string("paused"));
    RTF_ASSERT_EQUAL(e->start_ms, last);
}

void test_r_keep_awake::test_clock_back_is_no_gap()
{
    r_sleep_gap_detector d;
    int64_t t = 1700000000000;
    d.tick(t);
    t -= 3600 * SEC;
    RTF_ASSERT(!d.tick(t));
    t += 5 * SEC;
    RTF_ASSERT(!d.tick(t));
}

void test_r_keep_awake::test_settings_and_events_persist()
{
    auto top = _top_dir();
    {
        r_keep_awake ka(top);
        RTF_ASSERT(ka.get_settings() == r_keep_awake_settings());
        RTF_ASSERT(ka.get_status().events.empty());
        ka.set_settings({true, true});
        RTF_ASSERT(ka.get_settings() == r_keep_awake_settings({true, true}));
    }
    {
        r_keep_awake ka(top);
        RTF_ASSERT(ka.get_settings() == r_keep_awake_settings({true, true}));
    }

    // Events saved by an earlier run come back, newest first, skipping bad ones.
    auto events = top + PATH_SLASH + "config" + PATH_SLASH + "power_events.json";
    {
        ofstream f(events);
        f << "[{\"what\":\"asleep\",\"start_ms\":2000,\"end_ms\":3000},"
             "{\"what\":7},"
             "{\"what\":\"paused\",\"start_ms\":500,\"end_ms\":900}]";
    }
    r_keep_awake ka(top);
    auto s = ka.get_status();
    RTF_ASSERT_EQUAL(s.events.size(), (size_t)2);
    RTF_ASSERT_EQUAL(s.events[0].what, string("asleep"));
    RTF_ASSERT_EQUAL(s.events[0].start_ms, (int64_t)2000);
    RTF_ASSERT_EQUAL(s.events[1].what, string("paused"));
    RTF_ASSERT_EQUAL(s.events[1].end_ms, (int64_t)900);

    // A broken settings file falls back to the defaults.
    {
        ofstream f(top + PATH_SLASH + "config" + PATH_SLASH + "keep_awake.json");
        f << "{";
    }
    r_keep_awake ka2(top);
    RTF_ASSERT(ka2.get_settings() == r_keep_awake_settings());
}

void test_r_keep_awake::test_blocker_holds_and_releases()
{
#ifdef IS_MACOS
    // A real power assertion, released at once.
    r_sleep_blocker b;
    RTF_ASSERT(!b.held());
    RTF_ASSERT_EQUAL(b.hold(KEEP_AWAKE_REASON), string());
    RTF_ASSERT(b.held());
    RTF_ASSERT_EQUAL(b.hold(KEEP_AWAKE_REASON), string());
    b.release();
    RTF_ASSERT(!b.held());
    b.release();
    RTF_ASSERT(!b.held());
#endif
}
