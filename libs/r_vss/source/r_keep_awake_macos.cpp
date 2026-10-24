#ifdef IS_MACOS

#include "r_vss/r_keep_awake.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOMessage.h>
#include <IOKit/ps/IOPowerSources.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <atomic>
#include <cstdio>
#include <future>
#include <stdexcept>

using namespace r_vss;
using namespace std;

struct r_sleep_blocker::impl
{
    IOPMAssertionID id {kIOPMNullAssertionID};
};

r_sleep_blocker::r_sleep_blocker() :
    _impl(make_unique<impl>())
{
}

r_sleep_blocker::~r_sleep_blocker() noexcept
{
    release();
}

string r_sleep_blocker::hold(const string& reason)
{
    if(held())
        return string();

    CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, reason.c_str(), kCFStringEncodingUTF8);
    // Idle system sleep only: the display still sleeps.
    IOReturn r = IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleSystemSleep, kIOPMAssertionLevelOn, name, &_impl->id);
    if(name)
        CFRelease(name);
    if(r != kIOReturnSuccess)
    {
        _impl->id = kIOPMNullAssertionID;
        char buf[64];
        snprintf(buf, sizeof(buf), "macOS refused the power assertion (0x%x).", (unsigned)r);
        return buf;
    }
    return string();
}

void r_sleep_blocker::release()
{
    if(_impl->id != kIOPMNullAssertionID)
    {
        IOPMAssertionRelease(_impl->id);
        _impl->id = kIOPMNullAssertionID;
    }
}

bool r_sleep_blocker::held() const
{
    return _impl->id != kIOPMNullAssertionID;
}

r_power_source r_vss::current_power_source()
{
    CFTypeRef info = IOPSCopyPowerSourcesInfo();
    if(!info)
        return r_power_source::unknown;

    r_power_source source = r_power_source::unknown;
    // "AC Power", "Battery Power" or "UPS Power". A Mac without a battery says AC.
    CFStringRef type = IOPSGetProvidingPowerSourceType(info);
    if(type)
    {
        if(CFStringCompare(type, CFSTR(kIOPMACPowerKey), 0) == kCFCompareEqualTo)
            source = r_power_source::ac;
        else if(CFStringCompare(type, CFSTR(kIOPMBatteryPowerKey), 0) == kCFCompareEqualTo ||
                CFStringCompare(type, CFSTR(kIOPMUPSPowerKey), 0) == kCFCompareEqualTo)
            source = r_power_source::battery;
    }
    CFRelease(info);
    return source;
}

// IORegisterForSystemPower delivers on a run loop: the watcher runs one on a
// thread of its own.
struct r_sleep_watcher::impl
{
    function<void(bool)> cb;
    io_connect_t root_port {MACH_PORT_NULL};
    IONotificationPortRef notify_port {nullptr};
    io_object_t notifier {0};
    atomic<bool> stopping {false};
    thread th;

    static void on_power(void* refcon, io_service_t, natural_t type, void* arg)
    {
        auto self = (impl*)refcon;
        switch(type)
        {
            case kIOMessageCanSystemSleep:
                // Idle sleep asks first; the assertion is what says no.
                IOAllowPowerChange(self->root_port, (long)arg);
                break;
            case kIOMessageSystemWillSleep:
                self->cb(true);
                IOAllowPowerChange(self->root_port, (long)arg);
                break;
            case kIOMessageSystemHasPoweredOn:
                self->cb(false);
                break;
            default:
                break;
        }
    }
};

r_sleep_watcher::r_sleep_watcher(function<void(bool)> cb) :
    _impl(make_unique<impl>())
{
    _impl->cb = std::move(cb);

    promise<bool> started;
    auto started_f = started.get_future();
    auto p = _impl.get();
    _impl->th = thread([p, &started]() {
        p->root_port = IORegisterForSystemPower(p, &p->notify_port, &impl::on_power, &p->notifier);
        if(p->root_port == MACH_PORT_NULL)
        {
            started.set_value(false);
            return;
        }
        auto run_loop = CFRunLoopGetCurrent();
        CFRunLoopAddSource(run_loop, IONotificationPortGetRunLoopSource(p->notify_port), kCFRunLoopDefaultMode);
        started.set_value(true);
        // In 1 s slices, so stopping never waits on a loop that hasn't started.
        while(!p->stopping)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);

        CFRunLoopRemoveSource(run_loop, IONotificationPortGetRunLoopSource(p->notify_port), kCFRunLoopDefaultMode);
        IODeregisterForSystemPower(&p->notifier);
        IOServiceClose(p->root_port);
        IONotificationPortDestroy(p->notify_port);
    });

    if(!started_f.get())
    {
        _impl->th.join();
        throw runtime_error("IORegisterForSystemPower failed");
    }
}

r_sleep_watcher::~r_sleep_watcher() noexcept
{
    _impl->stopping = true;
    if(_impl->th.joinable())
        _impl->th.join();
}

#endif
