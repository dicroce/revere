
#ifndef r_utils_r_power_h
#define r_utils_r_power_h

#include "r_utils/r_macro.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace r_utils
{

// The OS's side of keeping a computer awake: asking it not to sleep, whether
// it runs on AC or battery, and hearing that it's about to sleep.
//
// The request (r_sleep_blocker::hold()):
//  - macOS: a PreventUserIdleSystemSleep power assertion.
//  - Windows: a PowerRequestSystemRequired power request.
//  - Linux with a desktop session (the Snap too): the desktop's own
//    inhibitor, GNOME's SessionManager (flag 4, suspend) or else freedesktop
//    PowerManagement (KDE, Xfce). It stops automatic suspend, not the user
//    choosing Suspend; under the Snap, screen-inhibit-control allows both.
//  - Linux with no session (a headless service): a logind "sleep" block
//    inhibitor, which needs a polkit rule letting the user hold one.
//
// Going to sleep (r_sleep_watcher): macOS IORegisterForSystemPower, Windows
// PowerRegisterSuspendResumeNotification, Linux logind PrepareForSleep.

enum class r_power_source
{
    ac,
    battery,
    unknown
};

R_API std::string power_source_name(r_power_source source);

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

// Implemented per platform (r_power_macos/windows/linux.cpp).
class r_sleep_blocker
{
public:
    R_API r_sleep_blocker();
    R_API ~r_sleep_blocker() noexcept;
    r_sleep_blocker(const r_sleep_blocker&) = delete;
    r_sleep_blocker& operator=(const r_sleep_blocker&) = delete;

    // Empty when the request is held, else why the OS refused it. Holding
    // again while held does nothing. reason is shown to the user as who holds
    // it.
    R_API std::string hold(const std::string& reason);
    R_API void release();
    // False again once the OS drops a request (on Linux, when the bus that
    // carried it goes away), so the caller asks again.
    R_API bool held() const;
    // The last hold() found nothing to ask rather than a refusal: a Linux
    // desktop with neither GNOME's nor freedesktop PowerManagement's
    // inhibitor. Always false on macOS and Windows.
    R_API bool unavailable() const;

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

}

#endif
