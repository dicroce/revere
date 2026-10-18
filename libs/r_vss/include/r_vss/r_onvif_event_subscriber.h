
#ifndef __r_vss_r_onvif_event_subscriber_h
#define __r_vss_r_onvif_event_subscriber_h

#include "r_vss/r_onvif_event_tracker.h"
#include "r_disco/r_camera.h"
#include "r_utils/r_macro.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace r_vss
{

// A camera-reported event span: motion or a class, on Revere's clock (camera
// clocks are often wrong), end_ms 0 while it is still going.
struct r_onvif_span
{
    std::string what;
    int64_t start_ms {0};
    int64_t end_ms {0};
};

struct r_onvif_events_status
{
    // "starting", "subscribed", "down" (retrying), "no events" (the camera has
    // no events service), "stopped".
    std::string state {"starting"};
    int port {0};
    std::string motion_topic;           // empty until the camera raises one
    std::vector<std::string> classes;   // detection classes the camera has reported on
    int64_t last_event_ms {0};          // when the camera last reported a change
    int64_t failures {0};
    std::string last_error;
};

// Holds one ONVIF pull point subscription for a camera and turns its events
// into spans (milestone 3, shadow mode: nothing here changes what Revere
// records; the spans are logged and kept for comparison with MOG2).
//
// Runs its own thread: find the device service, subscribe, pull every 2 s,
// renew at half the termination time, resubscribe with backoff (10 s doubling
// to 10 min) on failure. request_stop() returns at once; the destructor waits
// for the thread, at most about one pull or one connection attempt.
class r_onvif_event_subscriber
{
public:
    R_API explicit r_onvif_event_subscriber(const r_disco::r_camera& camera);
    R_API ~r_onvif_event_subscriber() noexcept;

    r_onvif_event_subscriber(const r_onvif_event_subscriber&) = delete;
    r_onvif_event_subscriber& operator=(const r_onvif_event_subscriber&) = delete;

    R_API void start();
    R_API void request_stop();

    R_API r_onvif_events_status status() const;

    // Spans that started at or after since_ms, oldest first.
    R_API std::vector<r_onvif_span> spans(int64_t since_ms = 0) const;

    // Whether Revere should run a subscriber for this camera: an ONVIF camera
    // (not one added by RTSP URL) with an address.
    R_API static bool wanted_for(const r_disco::r_camera& camera);

private:
    void _entry_point();
    void _apply(const std::vector<r_onvif_transition>& transitions);
    bool _wait(int seconds);  // false if stopping

    r_disco::r_camera _camera;
    std::string _tag;
    std::thread _th;
    std::atomic<bool> _running {false};
    mutable std::mutex _lock;
    std::condition_variable _cv;
    r_onvif_event_tracker _tracker;
    r_onvif_events_status _status;
    std::deque<r_onvif_span> _spans;
};

}

#endif
