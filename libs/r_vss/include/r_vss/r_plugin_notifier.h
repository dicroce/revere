#ifndef __r_vss_r_plugin_notifier_h
#define __r_vss_r_plugin_notifier_h

#include "r_utils/r_macro.h"
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
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
    // Drops what is still queued and waits for the call that is running, if any.
    R_API ~r_plugin_notifier();

    R_API void accept(bool on);
    R_API void post(std::function<void()> call);

    R_API size_t dropped() const;

private:
    void _run();

    mutable std::mutex _m;
    std::condition_variable _cv;
    std::deque<std::function<void()>> _q;
    size_t _max_pending;
    size_t _dropped {0};
    bool _accepting {false};
    bool _stop {false};
    std::thread _th;
};

}

#endif
