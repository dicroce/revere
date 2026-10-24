#ifndef __r_vss_r_keep_awake_h
#define __r_vss_r_keep_awake_h

#include "r_utils/r_macro.h"
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace r_vss
{

// Keeps the computer from going to sleep on its own while Revere records, and
// notes when it slept anyway.
//
// "Keep this computer awake" is on by default, but only while the computer is
// plugged in: a laptop on battery sleeps as usual unless "Also on battery" is
// on too. A computer with no battery counts as plugged in. Only idle sleep is
// held off. The display still turns off, and closing a laptop's lid, the
// power button or choosing Sleep still sleeps it.
//
// The request, named KEEP_AWAKE_REASON so the user can see who holds it:
//  - macOS: a PreventUserIdleSystemSleep power assertion.
//  - Windows: a PowerRequestSystemRequired power request.
//  - Linux: a logind "sleep" block inhibitor, plus GNOME's suspend inhibit
//    (flag 4) when there's a desktop session. A system service needs a polkit
//    rule for the logind one: --install-service writes it. Under the Snap,
//    which can't reach logind, GNOME's inhibit or the freedesktop
//    PowerManagement one (both allowed by screen-inhibit-control).
//
// Sleeps are noticed two ways. The OS says it's going to sleep (macOS
// IORegisterForSystemPower, Windows PowerRegisterSuspendResumeNotification,
// Linux logind PrepareForSleep), and a ticker every TICK_MS sees the wall
// clock jump past a gap. Each gap is logged and kept as a power event: an
// "asleep" one when the OS said it was going to sleep, a "paused" one when it
// didn't (a sleep the OS didn't tell us about, or Revere itself stalled).

static const char* const KEEP_AWAKE_REASON = "Revere is recording cameras";

enum class r_power_source
{
    ac,
    battery,
    unknown
};

R_API std::string power_source_name(r_power_source source);

struct r_keep_awake_settings
{
    bool enabled {true};
    bool on_battery {false};

    bool operator==(const r_keep_awake_settings& o) const { return enabled == o.enabled && on_battery == o.on_battery; }
    bool operator!=(const r_keep_awake_settings& o) const { return !(*this == o); }
};

// Whether to hold the request. An unknown power source is held, like AC: a
// computer we can't read is more often a desktop than a laptop.
R_API bool keep_awake_wanted(const r_keep_awake_settings& settings, r_power_source source);

// config/keep_awake.json: {"enabled": true, "on_battery": false}. A missing
// file, bad JSON or a missing or non-boolean key gives the default for it.
R_API r_keep_awake_settings parse_keep_awake_settings(const std::string& json_text);
R_API std::string keep_awake_settings_json(const r_keep_awake_settings& settings);

// One entry of /sys/class/power_supply on Linux.
struct r_power_supply
{
    std::string type;   // "Mains", "Battery", "USB", "UPS", ...
    bool online {false};
    std::string scope;  // "Device" for a mouse's or keyboard's battery, else empty.
};

// AC when anything that feeds the computer is online or there's no battery of
// its own; battery when it has one and nothing feeds it.
R_API r_power_source power_source_from_supplies(const std::vector<r_power_supply>& supplies);

// From Windows' SYSTEM_POWER_STATUS: ACLineStatus 0 is battery, 1 AC, 255
// unknown; BatteryFlag 128 is "no system battery" (a desktop: AC).
R_API r_power_source power_source_from_status(uint8_t ac_line_status, uint8_t battery_flag);

struct r_power_event
{
    std::string what;   // "asleep" or "paused"
    int64_t start_ms {0};
    int64_t end_ms {0};
};

// Turns ticks of the wall clock (and the OS saying it's about to sleep) into
// power events. No threads and no clock: the caller passes the times (ms).
class r_sleep_gap_detector
{
public:
    enum
    {
        GAP_MS = 15000
    };

    R_API void on_sleep(int64_t wall_ms);

    // A tick more than GAP_MS after the last one is a gap. It was "asleep"
    // from the moment the OS said so when that came after the last tick
    // before the gap, else "paused" from that last tick. The clock stepping
    // back is never a gap.
    R_API std::optional<r_power_event> tick(int64_t wall_ms);

private:
    int64_t _last_tick_ms {-1};
    int64_t _sleep_ms {-1};
};

// Implemented per platform (r_keep_awake_macos/windows/linux.cpp).
class r_sleep_blocker
{
public:
    R_API r_sleep_blocker();
    R_API ~r_sleep_blocker() noexcept;
    r_sleep_blocker(const r_sleep_blocker&) = delete;
    r_sleep_blocker& operator=(const r_sleep_blocker&) = delete;

    // Empty when the request is held, else why the OS refused it. Holding
    // again while held does nothing.
    R_API std::string hold(const std::string& reason);
    R_API void release();
    R_API bool held() const;

private:
    struct impl;
    std::unique_ptr<impl> _impl;
};

R_API r_power_source current_power_source();

// Calls cb(true) when the OS says it's going to sleep and cb(false) when it
// woke, on a thread of its own. Silently does nothing where the OS won't tell.
class r_sleep_watcher
{
public:
    R_API explicit r_sleep_watcher(std::function<void(bool)> cb);
    R_API ~r_sleep_watcher() noexcept;
    r_sleep_watcher(const r_sleep_watcher&) = delete;
    r_sleep_watcher& operator=(const r_sleep_watcher&) = delete;

private:
    struct impl;
    std::unique_ptr<impl> _impl;
};

struct r_keep_awake_status
{
    r_keep_awake_settings settings;
    r_power_source power {r_power_source::unknown};
    bool wanted {false};
    bool held {false};
    // Why the OS refused the request; empty when held or not wanted.
    std::string problem;
    // Newest first.
    std::vector<r_power_event> events;
};

// The setting, the request and the power events, for the desktop app, the web
// UI and headless alike. Settings are in <top_dir>/config/keep_awake.json and
// the last MAX_EVENTS events in <top_dir>/config/power_events.json.
class r_keep_awake
{
public:
    enum
    {
        TICK_MS = 5000,
        RETRY_MS = 60000,
        MAX_EVENTS = 50
    };

    R_API explicit r_keep_awake(const std::string& top_dir);
    R_API ~r_keep_awake() noexcept;
    r_keep_awake(const r_keep_awake&) = delete;
    r_keep_awake& operator=(const r_keep_awake&) = delete;

    R_API void start();
    R_API void stop();

    R_API r_keep_awake_settings get_settings() const;
    // Saves and applies at once.
    R_API void set_settings(const r_keep_awake_settings& settings);

    R_API r_keep_awake_status get_status() const;

private:
    void _entry_point();
    void _apply(bool force_retry);
    void _on_os_sleep(bool sleeping);
    void _add_event(const r_power_event& e);
    void _save_events() const;

    std::string _config_dir;
    std::string _settings_path;
    std::string _events_path;

    mutable std::mutex _mutex;
    std::condition_variable _cond;
    bool _running {false};
    bool _changed {false};
    std::thread _th;

    r_keep_awake_settings _settings;
    r_power_source _power {r_power_source::unknown};
    bool _wanted {false};
    // Whether _blocker holds the request, for other threads: only the ticker
    // thread calls the blocker.
    bool _held {false};
    std::string _problem;
    int64_t _last_try_ms {0};
    std::deque<r_power_event> _events;

    r_sleep_gap_detector _gaps;
    r_sleep_blocker _blocker;
    std::unique_ptr<r_sleep_watcher> _watcher;
};

}

#endif
