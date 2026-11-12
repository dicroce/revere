#ifndef r_vss_r_network_watch_h
#define r_vss_r_network_watch_h

#include <chrono>
#include <string>

namespace r_vss
{

// Has the computer's network changed? The stream keeper asks on every pass of its loop, which also wakes for each command,
// so the watch does its own timing: it looks at the network no more than once a second (the signature is a system call),
// and a change counts once the new signature has stayed the same for HOLD, since an interface coming up often goes through
// an address or two on the way. The first signature is the starting point, not a change. After a change every RTSP session
// rides a connection the camera may no longer be able to answer on, so the keeper ends them cleanly (TEARDOWN, sockets
// closed) and connects again, instead of waiting for each to go quiet.
class r_network_watch
{
public:
    using clock = std::chrono::steady_clock;

    static constexpr std::chrono::milliseconds CHECK_EVERY {1000};
    static constexpr std::chrono::milliseconds HOLD {3000};

    // True once, when the network has changed and stayed changed. `signature` (a callable returning the string) is called
    // only when a check is due.
    template <class Signature>
    bool poll(clock::time_point now, Signature signature)
    {
        if(_started && now - _checked < CHECK_EVERY)
            return false;
        _checked = now;
        const std::string sig = signature();
        if(!_started)
        {
            _started = true;
            _settled = sig;
            return false;
        }
        if(sig == _settled)
        {
            _has_pending = false;
            return false;
        }
        if(!_has_pending || sig != _pending)
        {
            _pending = sig;
            _has_pending = true;
            _pending_since = now;
            return false;
        }
        if(now - _pending_since < HOLD)
            return false;
        _settled = sig;
        _has_pending = false;
        return true;
    }

private:
    bool _started {false};
    clock::time_point _checked {};
    std::string _settled;
    bool _has_pending {false};
    std::string _pending;
    clock::time_point _pending_since {};
};

// The keeper's pass: when the network has changed, every stream is marked dead (its own teardown and reconnect follow, in
// the keeper's loop). Returns how many it dropped.
template <class Streams, class Signature>
size_t r_drop_streams_on_network_change(r_network_watch& watch, r_network_watch::clock::time_point now, Signature signature, Streams& streams)
{
    if(!watch.poll(now, signature))
        return 0;
    size_t n = 0;
    for(auto& s : streams)
    {
        s.second->drop();
        ++n;
    }
    return n;
}

}

#endif
