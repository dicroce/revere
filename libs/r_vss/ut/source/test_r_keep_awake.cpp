#include "test_r_keep_awake.h"
#include "r_vss/r_keep_awake.h"
#include "r_utils/r_file.h"
#include <filesystem>
#include <fstream>
#ifdef IS_WINDOWS
#include <process.h>
#else
#include <unistd.h>
#endif
#ifdef IS_LINUX
#include <gio/gio.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <thread>
#endif

using namespace r_vss;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_keep_awake);

static const int64_t SEC = 1000;

static long _pid()
{
#ifdef IS_WINDOWS
    return (long)_getpid();
#else
    return (long)getpid();
#endif
}

static string _top_dir()
{
    // Per process, so two runs at once don't share it.
    static const string dir = (filesystem::temp_directory_path() / ("test_r_keep_awake_" + to_string(_pid()))).string();
    return dir;
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

void test_r_keep_awake::test_busy_time_is_no_gap()
{
    r_sleep_gap_detector d;
    int64_t t = 1700000000000;
    d.tick(t);

    // Twenty seconds in a D-Bus call that timed out, then the usual wait.
    t += 20 * SEC;
    d.skip(20 * SEC);
    t += 5 * SEC;
    RTF_ASSERT(!d.tick(t));

    // Busy for 20 s of which the computer slept 60 s more: still a gap, of
    // the sleep only.
    auto before = t;
    t += 80 * SEC;
    d.skip(20 * SEC);
    auto e = d.tick(t);
    RTF_ASSERT(e.has_value());
    RTF_ASSERT_EQUAL(e->what, string("paused"));
    RTF_ASSERT_EQUAL(e->start_ms, before + 20 * SEC);
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

#ifdef IS_LINUX
namespace
{

// A stand-in for gnome-session on a private bus. Like the real one, it drops
// an inhibitor when its caller's bus name goes away.
struct fake_session_manager
{
    GMainContext* context {nullptr};
    GMainLoop* loop {nullptr};
    GDBusConnection* bus {nullptr};
    std::thread th;
    std::atomic<int> inhibits {0};
    std::atomic<int> uninhibits {0};
    std::atomic<int> vanished {0};
    std::atomic<uint32_t> flags {0};

    static void on_vanished(GDBusConnection*, const gchar*, gpointer self)
    {
        ((fake_session_manager*)self)->vanished++;
    }

    static void on_call(GDBusConnection* c, const gchar* sender, const gchar*, const gchar*, const gchar* method,
                        GVariant* params, GDBusMethodInvocation* inv, gpointer user_data)
    {
        auto self = (fake_session_manager*)user_data;
        if(g_strcmp0(method, "Inhibit") == 0)
        {
            const gchar* app = nullptr;
            guint32 xid = 0, f = 0;
            const gchar* why = nullptr;
            g_variant_get(params, "(&su&su)", &app, &xid, &why, &f);
            self->flags = f;
            self->inhibits++;
            g_bus_watch_name_on_connection(c, sender, G_BUS_NAME_WATCHER_FLAGS_NONE, nullptr, &on_vanished, self, nullptr);
            g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", (guint32)42));
        }
        else
        {
            self->uninhibits++;
            g_dbus_method_invocation_return_value(inv, nullptr);
        }
    }

    bool start(const std::string& address)
    {
        static const char* xml =
            "<node><interface name='org.gnome.SessionManager'>"
            "<method name='Inhibit'><arg type='s' direction='in'/><arg type='u' direction='in'/>"
            "<arg type='s' direction='in'/><arg type='u' direction='in'/><arg type='u' direction='out'/></method>"
            "<method name='Uninhibit'><arg type='u' direction='in'/></method>"
            "</interface></node>";
        context = g_main_context_new();
        loop = g_main_loop_new(context, FALSE);
        g_main_context_push_thread_default(context);
        bus = g_dbus_connection_new_for_address_sync(
            address.c_str(),
            (GDBusConnectionFlags)(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, nullptr);
        bool ok = false;
        if(bus)
        {
            GDBusNodeInfo* info = g_dbus_node_info_new_for_xml(xml, nullptr);
            static const GDBusInterfaceVTable vtable = {&on_call, nullptr, nullptr, {}};
            g_dbus_connection_register_object(bus, "/org/gnome/SessionManager", info->interfaces[0], &vtable, this, nullptr, nullptr);
            g_dbus_node_info_unref(info);
            GVariant* r = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                                      "RequestName", g_variant_new("(su)", "org.gnome.SessionManager", 4u),
                                                      G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr);
            if(r)
            {
                ok = true;
                g_variant_unref(r);
            }
        }
        g_main_context_pop_thread_default(context);
        th = std::thread([this]() {
            g_main_context_push_thread_default(context);
            g_main_loop_run(loop);
            g_main_context_pop_thread_default(context);
        });
        return ok;
    }

    ~fake_session_manager()
    {
        GSource* idle = g_idle_source_new();
        g_source_set_callback(idle, [](gpointer l) -> gboolean { g_main_loop_quit((GMainLoop*)l); return G_SOURCE_REMOVE; }, loop, nullptr);
        g_source_attach(idle, context);
        g_source_unref(idle);
        if(th.joinable())
            th.join();
        if(bus)
            g_object_unref(bus);
        g_main_loop_unref(loop);
        g_main_context_unref(context);
    }
};

}
#endif

void test_r_keep_awake::test_session_inhibit_survives()
{
#ifdef IS_LINUX
    // A private session bus, so the desktop path runs without a desktop.
    // Skipped where there's no dbus-daemon.
    auto listen = "--address=unix:tmpdir=" + filesystem::temp_directory_path().string();
    const gchar* argv[] = {"dbus-daemon", "--session", "--nofork", "--print-address=1", listen.c_str(), nullptr};
    GSubprocess* daemon = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE, nullptr);
    if(!daemon)
        return;
    // Stopped, and the environment put back, even when an assert throws.
    struct cleanup
    {
        GSubprocess* daemon;
        ~cleanup()
        {
            unsetenv("DBUS_SESSION_BUS_ADDRESS");
            g_subprocess_send_signal(daemon, SIGTERM);
            g_subprocess_wait(daemon, nullptr, nullptr);
            g_object_unref(daemon);
        }
    } stop_daemon {daemon};

    GDataInputStream* out = g_data_input_stream_new(g_subprocess_get_stdout_pipe(daemon));
    gchar* line = g_data_input_stream_read_line(out, nullptr, nullptr, nullptr);
    std::string address = line ? line : "";
    g_free(line);
    g_object_unref(out);
    RTF_ASSERT(!address.empty());

    fake_session_manager fake;
    RTF_ASSERT(fake.start(address));

    // The process's session bus singleton is made on first use, from this.
    setenv("DBUS_SESSION_BUS_ADDRESS", address.c_str(), 1);

    r_sleep_blocker b;
    RTF_ASSERT_EQUAL(b.hold(KEEP_AWAKE_REASON), string());
    RTF_ASSERT(b.held());
    RTF_ASSERT_EQUAL(fake.inhibits.load(), 1);
    RTF_ASSERT_EQUAL(fake.flags.load(), (uint32_t)4);

    // The caller is still on the bus, so the inhibitor stands.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    RTF_ASSERT_EQUAL(fake.vanished.load(), 0);

    b.release();
    RTF_ASSERT(!b.held());
    RTF_ASSERT_EQUAL(fake.uninhibits.load(), 1);
#endif
}
