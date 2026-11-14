#include "r_utils/r_app_instance.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include <cstdlib>
#include <thread>

#ifdef IS_WINDOWS
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

using namespace r_utils;
using namespace std;
using namespace std::chrono;

#ifdef IS_WINDOWS
static string _mutex_name(const string& name) { return "Global\\" + name + "AppMutex"; }
static string _quit_event_name(const string& name) { return "Global\\" + name + "QuitEvent"; }

static int64_t _read_pid(const string& lock_path)
{
    try
    {
        auto bytes = r_fs::read_file(lock_path);
        return atoll(string(bytes.begin(), bytes.end()).c_str());
    }
    catch(...)
    {
        return 0;
    }
}
#else
static atomic<bool> _sigterm_received {false};
static void _sigterm_handler(int) { _sigterm_received = true; }
#endif

r_app_instance::r_app_instance(const string& name, const string& lock_path)
{
#ifdef IS_WINDOWS
    HANDLE m = CreateMutexA(NULL, TRUE, _mutex_name(name).c_str());
    if(m && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(m);
        return;
    }
    _mutex = m;
    _held = true;
    _quit_event = CreateEventA(NULL, FALSE, FALSE, _quit_event_name(name).c_str());
    auto pid = r_string_utils::format("%lu", (unsigned long)GetCurrentProcessId());
    try { r_fs::write_file((const uint8_t*)pid.c_str(), pid.size(), lock_path); } catch(...) {}
#else
    (void)name;
    // O_CLOEXEC: a program the app starts (a browser) must not inherit the
    // lock, or the app can't start again until that program exits.
    _fd = open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if(_fd >= 0 && flock(_fd, LOCK_EX | LOCK_NB) != 0)
    {
        close(_fd);
        _fd = -1;
        return;
    }
    // A lock file that can't be opened at all leaves it unguarded, not refused.
    _held = true;
    if(_fd >= 0)
    {
        auto pid = r_string_utils::format("%ld", (long)getpid());
        if(ftruncate(_fd, 0) == 0)
            (void)!write(_fd, pid.c_str(), pid.size());
    }
    signal(SIGTERM, _sigterm_handler);
#endif
}

r_app_instance::~r_app_instance() noexcept
{
    release();
}

bool r_app_instance::quit_requested()
{
    if(_quit_seen)
        return true;
#ifdef IS_WINDOWS
    // An auto-reset event: seen once, so remembered.
    if(_quit_event && WaitForSingleObject((HANDLE)_quit_event, 0) == WAIT_OBJECT_0)
        _quit_seen = true;
#else
    if(_held && _sigterm_received)
        _quit_seen = true;
#endif
    return _quit_seen;
}

void r_app_instance::release() noexcept
{
#ifdef IS_WINDOWS
    if(_quit_event)
    {
        CloseHandle((HANDLE)_quit_event);
        _quit_event = nullptr;
    }
    // Not released, only closed: a waiting request_quit() sees the mutex
    // abandoned when this process exits, not before.
    if(_mutex)
    {
        CloseHandle((HANDLE)_mutex);
        _mutex = nullptr;
    }
#else
    if(_fd >= 0)
    {
        flock(_fd, LOCK_UN);
        close(_fd);
        _fd = -1;
    }
#endif
    _held = false;
}

bool r_app_instance::request_quit(const string& name, const string& lock_path, milliseconds grace, milliseconds force_wait)
{
#ifdef IS_WINDOWS
    HANDLE m = OpenMutexA(SYNCHRONIZE, FALSE, _mutex_name(name).c_str());
    if(!m)
        return true;  // not running

    HANDLE e = OpenEventA(EVENT_MODIFY_STATE, FALSE, _quit_event_name(name).c_str());
    if(e)
    {
        SetEvent(e);
        CloseHandle(e);
    }

    // The mutex is signalled (abandoned) when the instance exits.
    DWORD r = WaitForSingleObject(m, (DWORD)grace.count());
    CloseHandle(m);
    if(r != WAIT_TIMEOUT)
        return true;

    auto pid = _read_pid(lock_path);
    if(pid > 0)
    {
        HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
        if(p)
        {
            TerminateProcess(p, 1);
            CloseHandle(p);
        }
    }

    m = OpenMutexA(SYNCHRONIZE, FALSE, _mutex_name(name).c_str());
    if(!m)
        return true;
    r = WaitForSingleObject(m, (DWORD)force_wait.count());
    CloseHandle(m);
    return r != WAIT_TIMEOUT;
#else
    (void)name;
    int fd = open(lock_path.c_str(), O_RDWR | O_CLOEXEC, 0600);
    if(fd < 0)
        return true;  // no lock file: not running

    auto gone = [fd]() {
        if(flock(fd, LOCK_EX | LOCK_NB) != 0)
            return false;
        flock(fd, LOCK_UN);
        return true;
    };
    auto wait_gone = [&](milliseconds limit) {
        auto deadline = steady_clock::now() + limit;
        while(!gone())
        {
            if(steady_clock::now() >= deadline)
                return false;
            this_thread::sleep_for(milliseconds(100));
        }
        return true;
    };

    if(gone())
    {
        close(fd);
        return true;
    }

    char pid_buf[32] = {};
    (void)!pread(fd, pid_buf, sizeof(pid_buf) - 1, 0);
    pid_t pid = (pid_t)atoi(pid_buf);
    if(pid > 0)
        kill(pid, SIGTERM);

    bool done = wait_gone(grace);
    if(!done)
    {
        if(pid > 0)
            kill(pid, SIGKILL);
        done = wait_gone(force_wait);
    }
    close(fd);
    return done;
#endif
}
