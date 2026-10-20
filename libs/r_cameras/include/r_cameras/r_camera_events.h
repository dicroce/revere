
#ifndef __r_cameras_r_camera_events_h
#define __r_cameras_r_camera_events_h

#include "r_disco/r_camera.h"
#include "r_utils/r_macro.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// What Revere needs from a camera's own detection, whichever way the camera
// provides it (ONVIF pull point today; vendor APIs behind the same interface).
// Shaped by what Revere consumes, not by what cameras can do: motion and
// object classes starting and ending, and whether that is working.
namespace r_cameras
{

// A start or end the camera reported.
struct r_camera_transition
{
    std::string what;        // "motion", or a class: "person", "vehicle", "animal", "package", "visitor", "face"
    bool active {false};     // true = started, false = ended
    std::string topic;       // where it came from in the camera's terms (an ONVIF topic, an API field)
    std::string source;      // the camera's video source, when it has several ("" for motion)
};

// A camera-reported span: motion or a class, on Revere's clock (camera
// clocks are often wrong), end_ms 0 while it is still going.
struct r_camera_span
{
    std::string what;
    int64_t start_ms {0};
    int64_t end_ms {0};
};

struct r_camera_events_status
{
    std::string source;                 // where events come from: "onvif", "reolink"
    bool up {false};                    // events are flowing (subscribed, polling...)
    // The source's own word for it: for ONVIF "starting", "subscribed",
    // "down" (retrying), "no events" (the camera has no events service), "stopped".
    std::string state {"starting"};
    int port {0};
    std::string motion_topic;           // the camera's last motion topic or field to go active; empty until one has
    std::vector<std::string> classes;   // detection classes the camera has reported on
    int64_t last_event_ms {0};          // when the camera last reported a change
    int64_t failures {0};
    std::string last_error;
};

// One camera's events, on a thread of its own. start() begins; request_stop()
// returns at once; destroying it waits for the thread.
class r_camera_events
{
public:
    // Called with each start and end, on the events' thread, outside any lock.
    using transition_fn = std::function<void(const r_camera_transition&)>;

    virtual ~r_camera_events() noexcept = default;

    virtual void start() = 0;
    virtual void request_stop() = 0;
    virtual r_camera_events_status status() const = 0;

    // Spans that started at or after since_ms, oldest first.
    virtual std::vector<r_camera_span> spans(int64_t since_ms = 0) const = 0;
};

// Whether Revere can get events from this camera: an ONVIF camera (not one
// added by RTSP URL) with an address.
R_API bool camera_events_wanted(const r_disco::r_camera& camera);

// The events for a camera (null if camera_events_wanted() is false): ONVIF,
// with Reolink's API as the fallback (see r_camera_events_chain).
R_API std::unique_ptr<r_camera_events> make_camera_events(const r_disco::r_camera& camera, r_camera_events::transition_fn on_transition);

}

#endif
