#ifdef IS_WINDOWS

#include "r_vss/r_keep_awake.h"
#include <windows.h>
#include <powrprof.h>
#include <stdexcept>

using namespace r_vss;
using namespace std;

static wstring _widen(const string& s)
{
    if(s.empty())
        return wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

struct r_sleep_blocker::impl
{
    HANDLE request {INVALID_HANDLE_VALUE};
    // REASON_CONTEXT points at the string, so it lives as long as the request.
    wstring reason;
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

    _impl->reason = _widen(reason);
    REASON_CONTEXT ctx {};
    ctx.Version = POWER_REQUEST_CONTEXT_VERSION;
    ctx.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    ctx.Reason.SimpleReasonString = &_impl->reason[0];

    HANDLE h = PowerCreateRequest(&ctx);
    if(h == INVALID_HANDLE_VALUE)
        return "Windows refused the power request (error " + to_string(GetLastError()) + ").";

    // Shows in "powercfg /requests" under SYSTEM with the reason.
    if(!PowerSetRequest(h, PowerRequestSystemRequired))
    {
        auto err = GetLastError();
        CloseHandle(h);
        return "Windows refused the power request (error " + to_string(err) + ").";
    }
    _impl->request = h;
    return string();
}

void r_sleep_blocker::release()
{
    if(_impl->request != INVALID_HANDLE_VALUE)
    {
        PowerClearRequest(_impl->request, PowerRequestSystemRequired);
        CloseHandle(_impl->request);
        _impl->request = INVALID_HANDLE_VALUE;
    }
}

bool r_sleep_blocker::held() const
{
    return _impl->request != INVALID_HANDLE_VALUE;
}

bool r_sleep_blocker::unavailable() const
{
    return false;
}

r_power_source r_vss::current_power_source()
{
    SYSTEM_POWER_STATUS s {};
    if(!GetSystemPowerStatus(&s))
        return r_power_source::unknown;
    return power_source_from_status(s.ACLineStatus, s.BatteryFlag);
}

// Delivered on a system thread, no window or message loop needed.
struct r_sleep_watcher::impl
{
    function<void(bool)> cb;
    HPOWERNOTIFY handle {nullptr};
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params {};

    static ULONG CALLBACK on_power(PVOID context, ULONG type, PVOID)
    {
        auto self = (impl*)context;
        switch(type)
        {
            case PBT_APMSUSPEND:
                self->cb(true);
                break;
            case PBT_APMRESUMEAUTOMATIC:
                self->cb(false);
                break;
            default:
                break;
        }
        return ERROR_SUCCESS;
    }
};

r_sleep_watcher::r_sleep_watcher(function<void(bool)> cb) :
    _impl(make_unique<impl>())
{
    _impl->cb = std::move(cb);
    _impl->params.Callback = &impl::on_power;
    _impl->params.Context = _impl.get();
    auto err = PowerRegisterSuspendResumeNotification(DEVICE_NOTIFY_CALLBACK, &_impl->params, (PHPOWERNOTIFY)&_impl->handle);
    if(err != ERROR_SUCCESS)
        throw runtime_error("PowerRegisterSuspendResumeNotification failed (error " + to_string(err) + ")");
}

r_sleep_watcher::~r_sleep_watcher() noexcept
{
    if(_impl->handle)
        PowerUnregisterSuspendResumeNotification(_impl->handle);
}

#endif
