#ifdef IS_LINUX

#include "r_vss/r_keep_awake.h"
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <stdexcept>
#include <unistd.h>

using namespace r_vss;
using namespace std;

static string _gerror_text(GError* err)
{
    string s = (err && err->message) ? err->message : "unknown error";
    if(err)
        g_error_free(err);
    return s;
}

// A connection of Revere's own, not GLib's shared singleton for the bus. The
// singleton raises SIGTERM when the bus closes it (a logout, a D-Bus
// restart), which Revere takes as "quit", and once closed it's handed back
// closed. A private one does neither: a closed one counts as lost, and the
// next attempt opens a new one. It also keeps its own bus name for as long
// as Revere holds it, which a desktop inhibitor is tied to.
static GDBusConnection* _bus_open(GBusType type, GError** err)
{
    gchar* address = g_dbus_address_get_for_bus_sync(type, nullptr, err);
    if(!address)
        return nullptr;
    GDBusConnection* bus = g_dbus_connection_new_for_address_sync(
        address,
        (GDBusConnectionFlags)(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr, nullptr, err
    );
    g_free(address);
    if(bus)
        g_dbus_connection_set_exit_on_close(bus, FALSE);
    return bus;
}

static void _bus_close(GDBusConnection* bus)
{
    if(!bus)
        return;
    g_dbus_connection_close_sync(bus, nullptr, nullptr);
    g_object_unref(bus);
}

// logind's "sleep" block inhibitor: held while the fd it returns is open.
// Shows in "systemd-inhibit --list".
static int _logind_inhibit(const string& reason, string& problem)
{
    GError* err = nullptr;
    GDBusConnection* bus = _bus_open(G_BUS_TYPE_SYSTEM, &err);
    if(!bus)
    {
        problem = "no system bus: " + _gerror_text(err);
        return -1;
    }

    GUnixFDList* fds = nullptr;
    GVariant* reply = g_dbus_connection_call_with_unix_fd_list_sync(
        bus,
        "org.freedesktop.login1",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        "Inhibit",
        g_variant_new("(ssss)", "sleep", "Revere", reason.c_str(), "block"),
        G_VARIANT_TYPE("(h)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,
        &fds,
        nullptr,
        &err
    );
    _bus_close(bus);

    if(!reply)
    {
        problem = "logind refused: " + _gerror_text(err);
        return -1;
    }

    gint32 index = -1;
    g_variant_get(reply, "(h)", &index);
    g_variant_unref(reply);

    int fd = -1;
    if(fds)
    {
        fd = g_unix_fd_list_get(fds, index, &err);
        g_object_unref(fds);
        if(fd < 0)
            problem = "logind's inhibitor fd: " + _gerror_text(err);
    }
    else
        problem = "logind returned no inhibitor fd";
    return fd;
}

// A session-bus inhibitor that returns a cookie: GNOME's SessionManager
// (flags 4: suspend) or freedesktop PowerManagement (KDE, Xfce). Both drop the
// inhibitor when the caller's bus name goes away, so the connection that made
// the call is kept until Uninhibit.
struct session_inhibit
{
    GDBusConnection* bus {nullptr};
    string service;
    string path;
    string iface;
    string uninhibit;
    guint32 cookie {0};
};

static bool _session_call(GDBusConnection* bus, const char* service, const char* path, const char* iface,
                          GVariant* params, guint32& cookie, string& problem, bool& missing)
{
    GError* err = nullptr;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, service, path, iface, "Inhibit", params, G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, &err
    );
    if(!reply)
    {
        // Nothing on the bus by that name: not this desktop's inhibitor.
        missing = err && (g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
                          g_error_matches(err, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER));
        problem = _gerror_text(err);
        return false;
    }
    missing = false;
    g_variant_get(reply, "(u)", &cookie);
    g_variant_unref(reply);
    return true;
}

static bool _session_inhibit(const string& reason, session_inhibit& out, string& problem, bool& unavailable)
{
    GError* err = nullptr;
    GDBusConnection* bus = _bus_open(G_BUS_TYPE_SESSION, &err);
    if(!bus)
    {
        problem = "no desktop session bus: " + _gerror_text(err);
        return false;
    }

    // GNOME first: its idle suspend only checks its own inhibitors.
    string gnome_problem;
    bool gnome_missing = false;
    if(_session_call(bus, "org.gnome.SessionManager", "/org/gnome/SessionManager", "org.gnome.SessionManager",
                     g_variant_new("(susu)", "revere", (guint32)0, reason.c_str(), (guint32)4), out.cookie, gnome_problem, gnome_missing))
    {
        out.bus = bus;
        out.service = "org.gnome.SessionManager";
        out.path = "/org/gnome/SessionManager";
        out.iface = "org.gnome.SessionManager";
        out.uninhibit = "Uninhibit";
        return true;
    }

    string pm_problem;
    bool pm_missing = false;
    if(_session_call(bus, "org.freedesktop.PowerManagement", "/org/freedesktop/PowerManagement/Inhibit", "org.freedesktop.PowerManagement.Inhibit",
                     g_variant_new("(ss)", "Revere", reason.c_str()), out.cookie, pm_problem, pm_missing))
    {
        out.bus = bus;
        out.service = "org.freedesktop.PowerManagement";
        out.path = "/org/freedesktop/PowerManagement/Inhibit";
        out.iface = "org.freedesktop.PowerManagement.Inhibit";
        out.uninhibit = "UnInhibit";
        return true;
    }

    _bus_close(bus);
    // Neither is running (sway, i3, a bare X session): nothing refused, there's
    // just nobody to ask. That desktop's own settings decide when it sleeps.
    unavailable = gnome_missing && pm_missing;
    if(unavailable)
        problem = "this desktop has no suspend inhibitor to ask";
    else
        problem = "the desktop refused (GNOME: " + gnome_problem + "; PowerManagement: " + pm_problem + ")";
    return false;
}

static void _session_uninhibit(session_inhibit& s)
{
    if(!s.bus)
        return;
    GVariant* reply = g_dbus_connection_call_sync(
        s.bus, s.service.c_str(), s.path.c_str(), s.iface.c_str(), s.uninhibit.c_str(),
        g_variant_new("(u)", s.cookie), nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr
    );
    if(reply)
        g_variant_unref(reply);
    _bus_close(s.bus);
    s.bus = nullptr;
}

// A desktop session's bus: named in the environment, or systemd's per-user
// one. A system service has neither.
static bool _has_session_bus()
{
    auto address = getenv("DBUS_SESSION_BUS_ADDRESS");
    if(address && *address)
        return true;
    auto runtime = getenv("XDG_RUNTIME_DIR");
    return runtime && *runtime && access((string(runtime) + "/bus").c_str(), F_OK) == 0;
}

struct r_sleep_blocker::impl
{
    int logind_fd {-1};
    session_inhibit session;
    bool unavailable {false};
};

r_sleep_blocker::r_sleep_blocker() :
    _impl(make_unique<impl>())
{
}

r_sleep_blocker::~r_sleep_blocker() noexcept
{
    release();
}

// A desktop session (the Snap included) takes only the desktop's inhibitor:
// it stops automatic suspend but not the user choosing Suspend. A logind
// "sleep" block would make that Suspend ask for an admin password, and the
// Snap can't reach logind anyway. A service, with no session and nobody at
// the keyboard, takes logind's "sleep" block, allowed by the polkit rule that
// --install-service writes.
string r_sleep_blocker::hold(const string& reason)
{
    if(held())
        return string();

    // A hold the bus took away (see held()): tidy it up before asking again.
    release();

    const bool snap = getenv("SNAP") != nullptr;
    string problem;
    _impl->unavailable = false;
    if(snap || _has_session_bus())
    {
        if(!_session_inhibit(reason, _impl->session, problem, _impl->unavailable))
            return problem;
        return string();
    }

    _impl->logind_fd = _logind_inhibit(reason, problem);
    if(_impl->logind_fd >= 0)
        return string();
    if(getuid() != 0)
        problem += " (a service needs the polkit rule that revere --install-service writes)";
    return problem;
}

void r_sleep_blocker::release()
{
    if(_impl->logind_fd >= 0)
    {
        close(_impl->logind_fd);
        _impl->logind_fd = -1;
    }
    _session_uninhibit(_impl->session);
}

// A session inhibitor is lost with its connection: the desktop drops it when
// Revere's name leaves the bus. Not held then, so the ticker asks again.
bool r_sleep_blocker::held() const
{
    return _impl->logind_fd >= 0 || (_impl->session.bus && !g_dbus_connection_is_closed(_impl->session.bus));
}

bool r_sleep_blocker::unavailable() const
{
    return _impl->unavailable;
}

static string _read_line(const string& path)
{
    ifstream f(path);
    string line;
    if(f)
        getline(f, line);
    return line;
}

r_power_source r_vss::current_power_source()
{
    const string dir = "/sys/class/power_supply";
    DIR* d = opendir(dir.c_str());
    if(!d)
        return r_power_source::unknown;

    vector<r_power_supply> supplies;
    while(auto ent = readdir(d))
    {
        string name = ent->d_name;
        if(name == "." || name == "..")
            continue;
        auto base = dir + "/" + name + "/";
        r_power_supply s;
        s.type = _read_line(base + "type");
        s.online = _read_line(base + "online") == "1";
        s.scope = _read_line(base + "scope");
        if(!s.type.empty())
            supplies.push_back(s);
    }
    closedir(d);
    return power_source_from_supplies(supplies);
}

// logind's PrepareForSleep signal, on a GLib main loop of the watcher's own.
// Not delivered to the Snap (no login1 access): the ticker still sees the gap.
struct r_sleep_watcher::impl
{
    function<void(bool)> cb;
    GMainContext* context {nullptr};
    GMainLoop* loop {nullptr};
    GDBusConnection* bus {nullptr};
    guint subscription {0};
    thread th;

    static void on_signal(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* params, gpointer user_data)
    {
        gboolean sleeping = FALSE;
        g_variant_get(params, "(b)", &sleeping);
        ((impl*)user_data)->cb(sleeping == TRUE);
    }
};

r_sleep_watcher::r_sleep_watcher(function<void(bool)> cb) :
    _impl(make_unique<impl>())
{
    _impl->cb = std::move(cb);

    GError* err = nullptr;
    _impl->bus = _bus_open(G_BUS_TYPE_SYSTEM, &err);
    if(!_impl->bus)
        throw runtime_error("no system bus: " + _gerror_text(err));

    _impl->context = g_main_context_new();
    _impl->loop = g_main_loop_new(_impl->context, FALSE);

    // Signals go to the thread-default context at the time of subscribing.
    g_main_context_push_thread_default(_impl->context);
    _impl->subscription = g_dbus_connection_signal_subscribe(
        _impl->bus,
        "org.freedesktop.login1",
        "org.freedesktop.login1.Manager",
        "PrepareForSleep",
        "/org/freedesktop/login1",
        nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        &impl::on_signal,
        _impl.get(),
        nullptr
    );
    g_main_context_pop_thread_default(_impl->context);

    auto p = _impl.get();
    _impl->th = thread([p]() {
        g_main_context_push_thread_default(p->context);
        g_main_loop_run(p->loop);
        g_main_context_pop_thread_default(p->context);
    });
}

r_sleep_watcher::~r_sleep_watcher() noexcept
{
    if(_impl->bus && _impl->subscription)
        g_dbus_connection_signal_unsubscribe(_impl->bus, _impl->subscription);
    if(_impl->loop)
    {
        // Quit from inside the loop: a quit before g_main_loop_run starts
        // would be lost.
        GSource* idle = g_idle_source_new();
        g_source_set_callback(idle, [](gpointer loop) -> gboolean { g_main_loop_quit((GMainLoop*)loop); return G_SOURCE_REMOVE; }, _impl->loop, nullptr);
        g_source_attach(idle, _impl->context);
        g_source_unref(idle);
    }
    if(_impl->th.joinable())
        _impl->th.join();
    if(_impl->loop)
        g_main_loop_unref(_impl->loop);
    if(_impl->context)
        g_main_context_unref(_impl->context);
    if(_impl->bus)
        _bus_close(_impl->bus);
}

#endif
