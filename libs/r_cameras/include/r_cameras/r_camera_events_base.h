
#ifndef __r_cameras_r_camera_events_base_h
#define __r_cameras_r_camera_events_base_h

#include "r_cameras/r_camera_events.h"
#include "r_utils/r_macro.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace r_cameras
{

// What every source shares: the thread, stopping, waiting with backoff, and
// turning transitions into spans, status and log lines ("<source> events
// [<camera>]: motion start"). A source implements _run(), which loops until
// _running() is false.
class r_camera_events_base : public r_camera_events
{
public:
    R_API r_camera_events_base(const r_disco::r_camera& camera, transition_fn on_transition, const std::string& source);
    R_API ~r_camera_events_base() noexcept override;

    r_camera_events_base(const r_camera_events_base&) = delete;
    r_camera_events_base& operator=(const r_camera_events_base&) = delete;

    R_API void start() override;
    R_API void request_stop() override;
    R_API r_camera_events_status status() const override;
    R_API std::vector<r_camera_span> spans(int64_t since_ms = 0) const override;

protected:
    enum { MAX_SPANS = 500 };
    static constexpr int64_t MAX_EVENT_MS = 15 * 60 * 1000;   // longer than any real motion: an end was missed

    // The source's loop. A subclass's destructor must call _join() so the
    // thread is gone before the subclass's members are.
    virtual void _run() = 0;
    R_API void _join();

    bool _running() const { return _keep_running.load(); }

    // Waits up to seconds, returning early (false) if asked to stop.
    R_API bool _wait(int seconds);

    // Records transitions (spans, status, log) and passes them on.
    R_API void _apply(const std::vector<r_camera_transition>& transitions);

    // Whether a span has been open longer than any real event (its end was missed).
    R_API bool _stale() const;

    // Status, under the lock: a function that changes it.
    R_API void _update(const std::function<void(r_camera_events_status&)>& change);

    // "<source> events [<camera>]: " for this source's own log lines.
    const std::string& _prefix() const { return _log_prefix; }

    r_disco::r_camera _camera;

private:
    void _entry_point();

    transition_fn _on_transition;
    std::string _log_prefix;
    std::thread _th;
    std::atomic<bool> _keep_running {false};
    mutable std::mutex _lock;
    std::condition_variable _cv;
    r_camera_events_status _status;
    std::deque<r_camera_span> _spans;
};

}

#endif
