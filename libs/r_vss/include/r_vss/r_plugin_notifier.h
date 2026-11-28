#ifndef __r_vss_r_plugin_notifier_h
#define __r_vss_r_plugin_notifier_h

#include "r_utils/r_macro.h"
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace r_vss
{

// Calls into one system plugin on a thread of its own, so that a plugin that is slow to take a notification (motion
// started, a camera's state changed) holds up only its own later notifications, never the thread that has the news:
// the motion engine, a camera's event thread, the stream keeper's loop.
//
// post() never waits. It queues the call; if `max_pending` are already waiting (the plugin is far behind), the oldest
// is dropped to make room, and counted. Calls run one at a time in the order posted. Until accept(true), and after
// accept(false), post() queues nothing.
class r_plugin_notifier final
{
public:
    R_API explicit r_plugin_notifier(size_t max_pending = 1024);
    r_plugin_notifier(const r_plugin_notifier&) = delete;
    r_plugin_notifier& operator=(const r_plugin_notifier&) = delete;
    // Drops what is still queued and waits for the call that is running, if any, for at most 5 s (shutdown()).
    R_API ~r_plugin_notifier();

    // Drops what is queued, accepts nothing more, and waits up to `limit` for the call that is running. Returns true
    // when the thread has ended. Returns false when the call was still running: the thread is then let go (detached),
    // and it ends by itself if the call ever returns. It keeps only the notifier's shared state, never the notifier,
    // so the notifier can be destroyed at once. What the call itself uses (the plugin's code) is the caller's to keep
    // loaded: see running().
    R_API bool shutdown(std::chrono::milliseconds limit);

    // True while a call is inside the plugin (or the plugin's thread was let go with one still in it).
    R_API bool running() const;

    // accept(false) also waits for the call that is running, if any, so that when it returns nothing is inside the
    // plugin on this notifier's account: for at most `limit`, since a plugin that never returns must not hang the
    // stop. Returns false when the call was still running then. (accept(true), and a call from inside the running
    // call, never wait.)
    R_API bool accept(bool on, std::chrono::milliseconds limit = std::chrono::milliseconds(5000));
    R_API void post(std::function<void()> call);

    R_API size_t dropped() const;

private:
    // Everything the thread touches lives here, held by the thread too, so a thread that is let go outlives the
    // notifier safely.
    struct _state
    {
        mutable std::mutex m;
        std::condition_variable cv;
        std::condition_variable idle;
        std::deque<std::function<void()>> q;
        size_t max_pending {1};
        size_t dropped {0};
        bool accepting {false};
        bool stop {false};
        bool running {false};
        bool done {false};   // the thread has left its loop
    };
    static void _run(std::shared_ptr<_state> s);

    std::shared_ptr<_state> _s;
    std::thread _th;
};

}

#endif
