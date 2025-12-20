
#ifndef __r_onvif_r_onvif_cam_h
#define __r_onvif_r_onvif_cam_h

#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

namespace r_onvif
{

// Motion event callback: (motion_detected, timestamp_ms)
using motion_event_cb = std::function<void(bool, int64_t)>;

// Event topics the camera may support
enum class event_capability
{
    none = 0,
    motion_alarm = 1 << 0,          // tns1:VideoSource/MotionAlarm
    cell_motion_detector = 1 << 1,  // tns1:RuleEngine/CellMotionDetector/Motion
};

inline event_capability operator|(event_capability a, event_capability b)
{
    return static_cast<event_capability>(static_cast<int>(a) | static_cast<int>(b));
}

inline bool has_capability(event_capability caps, event_capability flag)
{
    return (static_cast<int>(caps) & static_cast<int>(flag)) != 0;
}

class r_onvif_cam
{
public:
    R_API r_onvif_cam(
        const std::string& host,
        int port,
        const std::string& protocol,
        const std::string& xaddrs,
        const r_utils::r_nullable<std::string>& username,
        const r_utils::r_nullable<std::string>& password
    );

    R_API ~r_onvif_cam() noexcept;

    r_onvif_cam(const r_onvif_cam&) = delete;
    r_onvif_cam& operator=(const r_onvif_cam&) = delete;

    // Query what event types the camera supports
    R_API event_capability get_event_capabilities();

    R_API bool supports_motion_events();

    // Start/stop the event subscription thread
    R_API bool start_motion_subscription(motion_event_cb cb);
    R_API void stop_motion_subscription();

    // Health check - returns true if event thread has failed
    R_API bool dead() const;

private:
    void _event_thread_entry();

    std::string _get_events_service_url();
    event_capability _get_event_properties();
    std::string _create_pull_point_subscription();
    void _pull_messages();
    void _renew_subscription();
    void _unsubscribe();

    bool _parse_motion_state(const std::string& notification_xml, bool& motion_detected, int64_t& timestamp_out);
    bool _near_termination() const;

    // Connection info
    std::string _host;
    int _port;
    std::string _protocol;
    std::string _xaddrs;
    r_utils::r_nullable<std::string> _username;
    r_utils::r_nullable<std::string> _password;

    // Cached service URLs
    std::string _events_service_url;

    // Subscription state
    std::string _pullpoint_url;
    std::string _subscription_id;
    std::chrono::steady_clock::time_point _termination_time;
    int _time_offset{0};  // Seconds offset between local time and camera time

    // Thread management
    std::thread _event_thread;
    std::atomic<bool> _running{false};
    motion_event_cb _motion_cb;

    // Health tracking
    mutable std::mutex _health_mutex;
    std::chrono::steady_clock::time_point _last_successful_poll;
    int _consecutive_errors{0};
    std::atomic<bool> _dead{false};

    static constexpr int MAX_CONSECUTIVE_ERRORS = 5;
    static constexpr std::chrono::seconds DEAD_TIMEOUT{60};

    r_utils::r_nullable<event_capability> _cached_capabilities;
};

}

#endif
