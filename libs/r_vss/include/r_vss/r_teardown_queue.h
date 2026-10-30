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

// Destroys objects on a thread of its own. The stream keeper hands it the
// recording contexts it removes: a context's teardown (its restream mount,
// its GStreamer source, its event subscription) can block, and when the
// keeper's own thread did it, one stuck camera stopped every other camera
// being reconnected or added, and stopped the keeper answering, until revere
// hung on quit.
//
// Each object is destroyed by dropping the queue's reference, in the order
// pushed. A key (the camera id) stays pending until its object is gone, so
// the keeper doesn't start a camera again while its old context still holds
// the camera's files.
class r_teardown_queue
{
public:
    struct stuck_item
    {
        std::string key;
        std::string name;
        int64_t ms {0};       // how long it has been tearing down
    };

    R_API r_teardown_queue();
    // Calls finish() with a short bound if it wasn't called.
    R_API ~r_teardown_queue() noexcept;

    r_teardown_queue(const r_teardown_queue&) = delete;
    r_teardown_queue& operator=(const r_teardown_queue&) = delete;

    R_API void push(const std::string& key, const std::string& name, std::shared_ptr<void> doomed);

    // Queued or being torn down.
    R_API bool pending(const std::string& key) const;

    // The one being torn down, if it has taken at least min_ms.
    R_API std::vector<stuck_item> stuck(int64_t min_ms) const;

    // Waits up to timeout for everything pushed to be destroyed, then ends the
    // thread. True when it all finished. False when a teardown is still stuck:
    // it is logged by name and the thread is left to finish on its own
    // (detached), so the process can still exit. Later pushes are refused.
    R_API bool finish(std::chrono::milliseconds timeout);

private:
    struct state;
    std::shared_ptr<state> _state;
    std::thread _th;
};

}

#endif
