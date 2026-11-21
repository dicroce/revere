#include "r_vss/r_restart_backoff.h"

using namespace r_vss;
using namespace std;
using namespace std::chrono;

void r_restart_backoff::ended(bool received_video, bool answered, clock::time_point now, const string& config)
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

    if(++_failures == 1)
        _first_failure = now;
    if(!answered && now - _first_failure < minutes(2))
        _wait = _failures == 1 ? seconds(1) : _failures == 2 ? seconds(2) : _failures == 3 ? seconds(4) : seconds(5);
    else
        _wait = _failures == 1 ? seconds(20) : _failures == 2 ? seconds(60) : seconds(120);
    _next_start = now + _wait;
}
