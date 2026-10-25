#include "tray_companion.h"
#include "tray_status.h"
#include "utils.h"
#include "r_http/r_client_request.h"
#include "r_http/r_client_response.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_startup.h"
#include <tray.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <thread>

#ifdef IS_WINDOWS
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

using namespace std;
using namespace r_utils;
using namespace r_http;

static const char* TRAY_APP_NAME = "Revere Tray";
static const char* TRAY_ARGS = "--tray";
static const char* WEB_UI_URL = "http://localhost:8088";
static const int REVERE_PORT = 8088;
static const auto POLL_INTERVAL = chrono::seconds(5);

static optional<string> _fetch_cameras()
{
    try
    {
        r_socket sok;
        sok.set_io_timeout(3000);
        sok.connect("127.0.0.1", REVERE_PORT);

        r_client_request req("127.0.0.1", REVERE_PORT);
        req.set_uri("/cameras");
        req.write_request(sok);

        r_client_response resp;
        resp.read_response(sok);
        if(resp.get_status() != 200)
            return nullopt;

        auto body = resp.get_body_as_string();
        if(body.is_null())
            return nullopt;
        return body.value();
    }
    catch(const exception&)
    {
        return nullopt;
    }
}

// Keeps a second `revere --tray` from adding a second icon. Separate from
// revere.lock, which the service or desktop app holds.
class tray_instance_lock
{
public:
    tray_instance_lock(const string& top_dir)
    {
#ifdef IS_WINDOWS
        _mutex = CreateMutexW(NULL, TRUE, L"Local\\RevereTrayMutex");
        _acquired = _mutex && GetLastError() != ERROR_ALREADY_EXISTS;
#else
        auto path = top_dir + PATH_SLASH + "revere-tray.lock";
        _fd = open(path.c_str(), O_CREAT | O_RDWR, 0600);
        _acquired = _fd >= 0 && flock(_fd, LOCK_EX | LOCK_NB) == 0;
#endif
    }
    ~tray_instance_lock()
    {
#ifdef IS_WINDOWS
        if(_mutex)
            CloseHandle(_mutex);
#else
        if(_fd >= 0)
            close(_fd);
#endif
    }
    tray_instance_lock(const tray_instance_lock&) = delete;
    tray_instance_lock& operator=(const tray_instance_lock&) = delete;

    bool acquired() const { return _acquired; }

private:
#ifdef IS_WINDOWS
    HANDLE _mutex {nullptr};
#else
    int _fd {-1};
#endif
    bool _acquired {false};
};

int revere::run_tray_companion(const string& icon_path)
{
    tray_instance_lock instance_lock(revere::top_dir());
    if(!instance_lock.acquired())
    {
        fprintf(stderr, "The Revere tray icon is already running.\n");
        return 1;
    }

    // Snap has a single autostart file, declared in snapcraft.yaml for the
    // desktop app; a second entry would overwrite it.
    const bool can_autostart = getenv("SNAP") == nullptr;
    auto exe_path = r_fs::current_exe_path();

    unique_ptr<Tray::Tray> tray;
    try
    {
        tray = make_unique<Tray::Tray>("Revere", icon_path);
    }
    catch(const exception& ex)
    {
        fprintf(stderr,
                "Couldn't create the Revere tray icon (%s).\n"
                "--tray needs a desktop session; on Linux check that WAYLAND_DISPLAY or DISPLAY is set.\n",
                ex.what());
        return 1;
    }

    auto status = summarize_cameras(nullopt);
    status.summary = "Checking Revere...";
    status.detail.clear();

    tray->setTooltip("Revere: " + status.summary);
    auto status_entry = tray->addEntry(Tray::Label(status.summary));
    tray->addEntry(Tray::Separator());
    tray->addEntry(Tray::Button("Open web UI", [] {
        revere::open_url_in_browser(WEB_UI_URL);
    }));
    if(can_autostart)
    {
        tray->addEntry(Tray::Toggle("Start at login", r_startup::is_autostart_enabled(TRAY_APP_NAME),
            [exe_path](bool on) {
                r_startup::set_autostart(on, TRAY_APP_NAME, exe_path, TRAY_ARGS);
            }));
    }
    tray->addEntry(Tray::Separator());
    atomic<bool> quit_requested {false};
    tray->addEntry(Tray::Button("Quit tray icon", [&] {
        quit_requested = true;
    }));

    mutex status_lock;
    condition_variable poll_wake;
    optional<tray_status> latest;

    thread poller([&] {
        unique_lock<mutex> g(status_lock);
        while(!quit_requested)
        {
            g.unlock();
            auto st = summarize_cameras(_fetch_cameras());
            g.lock();
            latest = st;
            poll_wake.wait_for(g, POLL_INTERVAL, [&] { return quit_requested.load(); });
        }
    });

    R_LOG_INFO("Revere tray icon started");

    while(!quit_requested)
    {
        optional<tray_status> fresh;
        {
            lock_guard<mutex> g(status_lock);
            fresh.swap(latest);
        }

        if(fresh && (fresh->summary != status.summary || fresh->detail != status.detail))
        {
            status = *fresh;
            auto line = status.detail.empty() ? status.summary : status.summary + " - " + status.detail;
            status_entry->setText(line);
            tray->setTooltip("Revere: " + line);
            tray->update();
        }

        tray->pump();
        this_thread::sleep_for(chrono::milliseconds(50));
    }

    {
        lock_guard<mutex> g(status_lock);
        poll_wake.notify_all();
    }
    poller.join();
    tray->exit();
    tray->pump();

    R_LOG_INFO("Revere tray icon stopped");
    return 0;
}
