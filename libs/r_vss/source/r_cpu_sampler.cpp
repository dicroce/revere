#include "r_vss/r_cpu_sampler.h"
#include "r_utils/r_platform.h"
#include <algorithm>
#include <cstdio>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

using namespace r_vss;
using namespace std;

bool r_vss::read_cpu_ticks(r_cpu_ticks& ticks)
{
#if defined(__APPLE__)
    natural_t n = 0;
    processor_info_array_t info = nullptr;
    mach_msg_type_number_t count = 0;
    if(host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &n, &info, &count) != KERN_SUCCESS)
        return false;
    uint64_t busy = 0, total = 0;
    for(natural_t i = 0; i < n; ++i)
    {
        const auto* c = &info[i * CPU_STATE_MAX];
        busy += (uint64_t)c[CPU_STATE_USER] + c[CPU_STATE_SYSTEM] + c[CPU_STATE_NICE];
        total += (uint64_t)c[CPU_STATE_USER] + c[CPU_STATE_SYSTEM] + c[CPU_STATE_NICE] + c[CPU_STATE_IDLE];
    }
    vm_deallocate(mach_task_self(), (vm_address_t)info, count * sizeof(integer_t));
    ticks.busy = busy;
    ticks.total = total;
    return total > 0;
#elif defined(_WIN32)
    FILETIME idle, kernel, user;
    if(!GetSystemTimes(&idle, &kernel, &user))
        return false;
    auto u64 = [](const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; };
    // Kernel time includes idle time.
    const uint64_t total = u64(kernel) + u64(user);
    const uint64_t idle_t = u64(idle);
    ticks.total = total;
    ticks.busy = total > idle_t ? total - idle_t : 0;
    return total > 0;
#else
    FILE* f = fopen("/proc/stat", "r");
    if(!f)
        return false;
    unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
    // "cpu " is the line summed over the cores.
    const int got = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    fclose(f);
    if(got < 4)
        return false;
    ticks.busy = user + nice + system + irq + softirq + steal;
    ticks.total = ticks.busy + idle + iowait;
    return ticks.total > 0;
#endif
}

int r_vss::cpu_core_count()
{
    const unsigned n = thread::hardware_concurrency();
    return n > 0 ? (int)n : 1;
}

void r_cpu_sampler::add(int64_t now_ms, const r_cpu_ticks& ticks)
{
    if(!_have_last)
    {
        _last = ticks;
        _have_last = true;
        return;
    }
    // Counters that went back (a reset), or didn't move (no time passed): no reading.
    if(ticks.total <= _last.total || ticks.busy < _last.busy)
    {
        if(ticks.total < _last.total)
            _last = ticks;
        return;
    }
    const double busy = std::min(1.0, (double)(ticks.busy - _last.busy) / (double)(ticks.total - _last.total));
    _last = ticks;

    _window.push_back({now_ms, busy});
    while(!_window.empty() && now_ms - _window.front().at_ms > AVERAGE_WINDOW_MS)
        _window.pop_front();

    const double avg = this->busy();
    auto hold = [&](int64_t& since, double line) {
        if(avg >= line)
        {
            if(since < 0)
                since = now_ms;
        }
        else
            since = -1;
    };
    hold(_over_elevated_since, ELEVATED_BUSY);
    hold(_over_high_since, HIGH_BUSY);

    // The day's per-minute averages.
    const int64_t minute = now_ms / 60000;
    if(_minute >= 0 && minute != _minute && _minute_n > 0)
    {
        _minutes.push_back(_minute_sum / _minute_n);
        while(_minutes.size() > DAY_MINUTES)
            _minutes.pop_front();
        _minute_sum = 0.0;
        _minute_n = 0;
    }
    _minute = minute;
    _minute_sum += busy;
    ++_minute_n;
}

double r_cpu_sampler::busy() const
{
    if(_window.empty())
        return -1.0;
    double sum = 0.0;
    for(const auto& s : _window)
        sum += s.busy;
    return sum / (double)_window.size();
}

int64_t r_cpu_sampler::elevated_for_ms(int64_t now_ms) const
{
    return _over_elevated_since < 0 ? 0 : now_ms - _over_elevated_since;
}

int64_t r_cpu_sampler::high_for_ms(int64_t now_ms) const
{
    return _over_high_since < 0 ? 0 : now_ms - _over_high_since;
}

double r_cpu_sampler::busy_p95_24h() const
{
    if(_minutes.empty())
        return -1.0;
    vector<double> v(_minutes.begin(), _minutes.end());
    sort(v.begin(), v.end());
    // The nearest rank: the smallest value with 95% of them at or below it.
    size_t rank = (size_t)((double)v.size() * 0.95 + 0.999999);
    if(rank < 1)
        rank = 1;
    return v[std::min(rank, v.size()) - 1];
}
