
#ifndef r_utils_r_instance_lock_h
#define r_utils_r_instance_lock_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <string>

namespace r_utils
{

// One running instance per lock path, across processes. The holder keeps an
// OS lock for as long as it's alive (flock on Linux/macOS, a named mutex on
// Windows), so the OS releases it when the process exits or crashes: a lock
// file left behind is never mistaken for a running instance. The holder's pid
// is written into the file so another process can hand it the foreground.
//
// A process that finds the lock taken can ask the holder to show itself
// (notify()); the holder sees that with notified(), which it polls.
class r_instance_lock final
{
public:
    // Tries to take the lock at path; never blocks. See held().
    R_API explicit r_instance_lock(const std::string& path);
    r_instance_lock(const r_instance_lock&) = delete;
    r_instance_lock& operator=(const r_instance_lock&) = delete;
    R_API ~r_instance_lock() noexcept;

    // True when this process is the instance.
    bool held() const { return _held; }
    // When not held: the pid of the process that holds it, 0 if unknown.
    int64_t holder_pid() const { return _holder_pid; }

    // Asks the instance holding path to show itself.
    R_API static void notify(const std::string& path);
    // For the holder: true once for each notify() since the last call.
    R_API bool notified();

private:
    std::string _path;
    bool _held {false};
    int64_t _holder_pid {0};
    std::string _last_request;
#ifdef IS_WINDOWS
    void* _mutex {nullptr};
#else
    int _fd {-1};
#endif
};

}

#endif
