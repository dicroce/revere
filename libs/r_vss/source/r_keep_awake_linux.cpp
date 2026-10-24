#ifdef IS_LINUX

#include "r_vss/r_keep_awake.h"
#include "r_utils/r_logger.h"
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

// logind's "sleep" block inhibitor: held while the fd it returns is open.
// Shows in "systemd-inhibit --list".
static int _logind_inhibit(const string& reason, string& problem)
{
    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
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
    g_object_unref(bus);

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
// (flags 4: suspend) or freedesktop PowerManagement (KDE and others).
struct session_inhibit
{
    string service;
    string path;
    string iface;
    string uninhibit;
    guint32 cookie {0};
};

static bool _session_inhibit(const string& reason, session_inhibit& out, string& problem)
{
    GError* err = nullptr;
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if(!bus)
    {
        problem = "no desktop session bus: " + _gerror_text(err);
        return false;
    }

    // GNOME first: its idle suspend only checks its own inhibitors.
    GVariant* reply = g_dbus_connection_call_sync(
        bus,
        "org.gnome.SessionManager",
        "/org/gnome/SessionManager",
        "org.gnome.SessionManager",
        "Inhibit",
        g_variant_new("(susu)", "revere", (guint32)0, reason.c_str(), (guint32)4),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,
        &err
    );
    if(reply)
    {
        g_variant_get(reply, "(u)", &out.cookie);
        g_variant_unref(reply);
        out.service = "org.gnome.SessionManager";
        out.path = "/org/gnome/SessionManager";
        out.iface = "org.gnome.SessionManager";
        out.uninhibit = "Uninhibit";
        g_object_unref(bus);
        return true;
    }
    auto gnome_problem = _gerror_text(err);
    err = nullptr;

    reply = g_dbus_connection_call_sync(
        bus,
        "org.freedesktop.PowerManagement",
        "/org/freedesktop/PowerManagement/Inhibit",
        "org.freedesktop.PowerManagement.Inhibit",
        "Inhibit",
        g_variant_new("(ss)", "Revere", reason.c_str()),
        G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,
        &err
    );
    g_object_unref(bus);
    if(reply)
    {
        g_variant_get(reply, "(u)", &out.cookie);
        g_variant_unref(reply);
        out.service = "org.freedesktop.PowerManagement";
        out.path = "/org/freedesktop/PowerManagement/Inhibit";
        out.iface = "org.freedesktop.PowerManagement.Inhibit";
        out.uninhibit = "UnInhibit";
        return true;
    }
    problem = "the desktop refused (GNOME: " + gnome_problem + "; PowerManagement: " + _gerror_text(err) + ")";
    return false;
}

static void _session_uninhibit(const session_inhibit& s)
{
    GDBusConnection* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if(!bus)
        return;
    GVariant* reply = g_dbus_connection_call_sync(
        bus, s.service.c_str(), s.path.c_str(), s.iface.c_str(), s.uninhibit.c_str(),
        g_variant_new("(u)", s.cookie), nullptr, G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, nullptr
    );
    if(reply)
        g_variant_unref(reply);
    g_object_unref(bus);
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
    bool has_session {false};
    session_inhibit session;
};

r_sleep_blocker::r_sleep_blocker() :
    _impl(make_unique<impl>())
{
}

r_sleep_blocker::~r_sleep_blocker() noexcept
{
    release();
}

string r_sleep_blocker::hold(const string& reason)
{
    if(held())
        return string();

    // The Snap can't reach logind (that needs the shutdown interface, which
    // isn't granted to store snaps): only the session inhibitors.
    const bool snap = getenv("SNAP") != nullptr;

    string logind_problem;
    if(!snap)
        _impl->logind_fd = _logind_inhibit(reason, logind_problem);

    // A desktop also gets the session inhibitor, so its own idle suspend
    // doesn't try (GNOME's checks its inhibitors, not logind's). A service has
    // no session bus: logind alone.
    string session_problem;
    const bool want_session = snap || _has_session_bus();
    if(want_session)
        _impl->has_session = _session_inhibit(reason, _impl->session, session_problem);

    if(held())
    {
        if(!logind_problem.empty())
            R_LOG_WARNING("Keep awake: %s (holding the desktop's inhibitor only).", logind_problem.c_str());
        else if(want_session && !_impl->has_session)
            R_LOG_INFO("Keep awake: %s (holding logind's inhibitor only).", session_problem.c_str());
        return string();
    }

    string problem;
    if(!logind_problem.empty())
        problem = logind_problem;
    if(!session_problem.empty())
        problem += (problem.empty() ? "" : "; ") + session_problem;
    if(!snap && getuid() != 0 && !want_session)
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
    if(_impl->has_session)
    {
        _session_uninhibit(_impl->session);
        _impl->has_session = false;
    }
}

bool r_sleep_blocker::held() const
{
    return _impl->logind_fd >= 0 || _impl->has_session;
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
    _impl->bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &err);
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
        g_object_unref(_impl->bus);
}

#endif
