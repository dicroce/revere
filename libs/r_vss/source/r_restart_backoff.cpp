#include "r_vss/r_restart_backoff.h"

using namespace r_vss;
using namespace std;
using namespace std::chrono;

void r_restart_backoff::ended(bool received_video, bool answered, milliseconds streamed_for, clock::time_point now, const string& config)
{
    // New settings: what failed before says nothing about these.
    if(config != _config)
    {
        _failures = 0;
        _short_sessions = 0;
    }
    _config = config;

    if(received_video)
    {
        _failures = 0;
        if(streamed_for >= LONG_SESSION)
            _short_sessions = 0;
        else
            ++_short_sessions;
        // At once, then 2 s, 4 s, 8 s, ... up to SHORT_SESSION_MAX_WAIT.
        _wait = (_short_sessions <= 1) ? seconds(0) : min(SHORT_SESSION_MAX_WAIT, seconds(1LL << min(_short_sessions - 1, 6)));
        _next_start = now + _wait;
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
