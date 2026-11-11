#include "r_vss/r_restart_backoff.h"

using namespace r_vss;
using namespace std;
using namespace std::chrono;

void r_restart_backoff::ended(bool received_video, clock::time_point now, const string& config)
{
    // New settings: what failed before says nothing about these.
    if(config != _config)
        _failures = 0;
    _config = config;

    if(received_video)
    {
        _failures = 0;
        _wait = seconds(0);
        _next_start = now;
        return;
    }

    ++_failures;
    _wait = _failures == 1 ? seconds(20) : _failures == 2 ? seconds(60) : seconds(120);
    _next_start = now + _wait;
}
