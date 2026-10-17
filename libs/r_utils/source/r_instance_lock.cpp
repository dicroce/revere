
#include "r_utils/r_instance_lock.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <vector>

#ifdef IS_WINDOWS
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

using namespace r_utils;
using namespace std;

static string _request_path(const string& path)
{
    return path + ".notify";
}

static string _read_text(const string& path)
{
    try
    {
        if(!r_fs::file_exists(path))
            return string();
        auto bytes = r_fs::read_file(path);
        return string(bytes.begin(), bytes.end());
    }
    catch(...)
    {
        return string();
    }
}

static int64_t _current_pid()
{
#ifdef IS_WINDOWS
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}

r_instance_lock::r_instance_lock(const string& path) :
    _path(path)
{
#ifdef IS_WINDOWS
    // A named mutex rather than a file lock: Windows destroys it with its last
    // handle, so a crashed holder leaves nothing behind.
    string name = "Local\\r_instance_lock_";
    for(char c : path)
        name += (isalnum((unsigned char)c) ? c : '_');
    if(name.size() > 250)
        name = name.substr(name.size() - 250);
    HANDLE h = CreateMutexA(NULL, FALSE, name.c_str());
    if(h && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        _mutex = h;
        _held = true;
    }
    else if(h)
        CloseHandle(h);
#else
    _fd = open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if(_fd >= 0 && flock(_fd, LOCK_EX | LOCK_NB) == 0)
        _held = true;
#endif

    if(_held)
    {
        auto pid = r_string_utils::format("%lld", (long long)_current_pid());
#ifdef IS_WINDOWS
        try { r_fs::write_file((const uint8_t*)pid.c_str(), pid.size(), path); } catch(...) {}
#else
        if(ftruncate(_fd, 0) == 0)
            (void)!write(_fd, pid.c_str(), pid.size());
#endif
        // Requests from before we held it aren't for us.
        _last_request = _read_text(_request_path(path));
    }
    else
    {
#ifndef IS_WINDOWS
        if(_fd >= 0)
        {
            close(_fd);
            _fd = -1;
        }
#endif
        _holder_pid = atoll(_read_text(path).c_str());
    }
}

r_instance_lock::~r_instance_lock() noexcept
{
#ifdef IS_WINDOWS
    if(_mutex)
        CloseHandle((HANDLE)_mutex);
#else
    if(_fd >= 0)
    {
        flock(_fd, LOCK_UN);
        close(_fd);
    }
#endif
}

void r_instance_lock::notify(const string& path)
{
    // Unique each time, so the holder can tell a new request from the last.
    static atomic<uint64_t> n {0};
    auto nonce = r_string_utils::format("%lld-%llu-%llu",
        (long long)_current_pid(),
        (unsigned long long)chrono::steady_clock::now().time_since_epoch().count(),
        (unsigned long long)++n);
    try
    {
        r_fs::write_file((const uint8_t*)nonce.c_str(), nonce.size(), _request_path(path));
    }
    catch(...) {}
}

bool r_instance_lock::notified()
{
    if(!_held)
        return false;
    auto request = _read_text(_request_path(_path));
    if(request.empty() || request == _last_request)
        return false;
    _last_request = request;
    return true;
}
