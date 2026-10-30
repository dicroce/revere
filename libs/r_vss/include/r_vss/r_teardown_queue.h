#ifndef __r_vss_r_teardown_queue_h
#define __r_vss_r_teardown_queue_h

#include "r_utils/r_macro.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace r_vss
{

// Destroys objects on threads of its own. The stream keeper hands it the
// recording contexts it removes: a context's teardown (its restream mount,
// its GStreamer source, its event subscription) can block, and when the
// keeper's own thread did it, one stuck camera stopped every other camera
// being reconnected or added, and stopped the keeper answering, until revere
// hung on quit.
//
// Each object is destroyed by dropping the queue's reference, started in the
// order pushed, on up to THREADS threads at once, so one stuck teardown
// doesn't hold up the others. A key (the camera id) stays pending until its
// object is gone, so the keeper doesn't start a camera again while its old
// context still holds the camera's files.
class r_teardown_queue
{
public:
    struct stuck_item
    {
        std::string key;
        std::string name;
        int64_t ms {0};       // since it was pushed
        bool started {false}; // tearing down, or still waiting for a thread
    };

    enum { THREADS = 4 };

    R_API r_teardown_queue();
    // Calls finish() with a short bound if it wasn't called.
    R_API ~r_teardown_queue() noexcept;

    r_teardown_queue(const r_teardown_queue&) = delete;
    r_teardown_queue& operator=(const r_teardown_queue&) = delete;

    R_API void push(const std::string& key, const std::string& name, std::shared_ptr<void> doomed);

    // Queued or being torn down.
    R_API bool pending(const std::string& key) const;

    // Every one pending (queued or tearing down) for at least min_ms.
    R_API std::vector<stuck_item> stuck(int64_t min_ms) const;

    // Waits up to timeout for everything pushed to be destroyed, then ends the
    // threads. True when it all finished. False when one is still stuck: for
    // revere's exit. Each left is logged by name; one tearing down is left to
    // its thread (detached), and one not yet started is abandoned (leaked, never
    // destroyed: its destructor would run against a keeper and a GStreamer
    // that are going away). Later pushes are destroyed on the caller's thread.
    R_API bool finish(std::chrono::milliseconds timeout);

private:
    struct state;
    std::shared_ptr<state> _state;
    std::vector<std::thread> _threads;
};

}

#endif
