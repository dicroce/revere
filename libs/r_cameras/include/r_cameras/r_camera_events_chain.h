
#ifndef __r_cameras_r_camera_events_chain_h
#define __r_cameras_r_camera_events_chain_h

#include "r_cameras/r_camera_events.h"
#include "r_utils/r_macro.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace r_cameras
{

// A primary event source (ONVIF) with a fallback (a vendor API) for when the
// primary stops working. Reolinks' ONVIF service stops answering for hours
// while their video and their own API carry on (Driveway, all of 2026-10-19).
//
//  - The primary runs all the time.
//  - Once it has been down FALLBACK_AFTER_MS, the fallback is tried, if the
//    camera passes probe() (e.g. its Reolink API answers); a camera that fails
//    the probe is asked again every PROBE_EVERY_MS.
//  - When the primary is up again, the fallback stops (its open events end)
//    and the primary's own state takes over.
//
// Status is the fallback's while it runs, else the primary's. Spans are both
// sources', the fallback's kept after it stops.
class r_camera_events_chain final : public r_camera_events
{
public:
    using factory_fn = std::function<std::unique_ptr<r_camera_events>(transition_fn)>;
    using probe_fn = std::function<bool()>;

    enum
    {
        FALLBACK_AFTER_MS = 30000,
        PROBE_EVERY_MS = 5 * 60 * 1000,
        CHECK_MS = 2000,
        MAX_RETIRED_SPANS = 500
    };

    R_API r_camera_events_chain(factory_fn primary, factory_fn fallback, probe_fn probe,
                                transition_fn on_transition, const std::string& tag);
    R_API ~r_camera_events_chain() noexcept override;

    r_camera_events_chain(const r_camera_events_chain&) = delete;
    r_camera_events_chain& operator=(const r_camera_events_chain&) = delete;

    // start() also runs a thread that calls tick() every CHECK_MS.
    R_API void start() override;
    R_API void request_stop() override;
    R_API r_camera_events_status status() const override;
    R_API std::vector<r_camera_span> spans(int64_t since_ms = 0) const override;

    // One check: starts or stops the fallback as the primary's state says.
    // Public for tests, which call it with their own clock and no thread.
    R_API void tick(int64_t now_ms);

    // Starts the primary without the checking thread (for tests).
    R_API void start_without_thread();

    R_API bool fallback_running() const;

private:
    void _entry_point();
    void _retire_fallback();

    factory_fn _make_primary;
    factory_fn _make_fallback;
    probe_fn _probe;
    transition_fn _on_transition;
    std::string _tag;

    mutable std::mutex _lock;
    std::condition_variable _cv;
    bool _running {false};
    std::thread _th;
    std::unique_ptr<r_camera_events> _primary;
    std::unique_ptr<r_camera_events> _fallback;
    std::deque<r_camera_span> _retired;     // spans of fallbacks that have stopped

    int64_t _primary_down_since {-1};       // -1 = up (or not yet seen down)
    int _probe_result {-1};                 // -1 not asked, 0 failed, 1 passed
    int64_t _last_probe_ms {0};
};

}

#endif
