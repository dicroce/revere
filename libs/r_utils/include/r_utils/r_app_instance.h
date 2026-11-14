
#ifndef r_utils_r_app_instance_h
#define r_utils_r_app_instance_h

#include "r_utils/r_macro.h"
#include <atomic>
#include <chrono>
#include <string>

namespace r_utils
{

// The one running copy of an app, and the way another process (an installer)
// asks it to quit.
//
// Windows: holding the named mutex "Global\<name>AppMutex" makes this the
// instance; setting the named event "Global\<name>QuitEvent" asks it to quit.
// Linux/macOS: an flock on lock_path makes this the instance; SIGTERM asks it to
// quit. Either way the instance's pid is written to lock_path, so a quit that
// is ignored can be forced.
//
// The names are part of the protocol: an older build's request_quit() must
// still find a newer instance, and the other way round.
class r_app_instance final
{
public:
    // Tries to become the instance; never blocks. See held().
    R_API r_app_instance(const std::string& name, const std::string& lock_path);
    r_app_instance(const r_app_instance&) = delete;
    r_app_instance& operator=(const r_app_instance&) = delete;
    R_API ~r_app_instance() noexcept;

    // True when this process is the instance.
    bool held() const { return _held; }

    // True once a quit has been asked for. Poll it; it stays true.
    R_API bool quit_requested();

    // Stops being the instance. A request_quit() waiting on it sees it gone
    // once this process has exited.
    R_API void release() noexcept;

    // Asks the running instance to quit and waits up to grace for it to go.
    // If it doesn't, ends it (TerminateProcess / SIGKILL) and waits up to
    // force_wait more. True when no instance is left running (or none was).
    R_API static bool request_quit(const std::string& name,
                                   const std::string& lock_path,
                                   std::chrono::milliseconds grace,
                                   std::chrono::milliseconds force_wait);

private:
    bool _held {false};
    std::atomic<bool> _quit_seen {false};
#ifdef IS_WINDOWS
    void* _mutex {nullptr};
    void* _quit_event {nullptr};
#else
    int _fd {-1};
#endif
};

}

#endif
