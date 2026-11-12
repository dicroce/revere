#ifndef r_vss_r_network_watch_h
#define r_vss_r_network_watch_h

#include <string>

namespace r_vss
{

// Has the computer's network changed? Fed r_utils::r_networking::r_network_signature() on each pass of the stream keeper's
// loop. A change counts once the new signature has been seen on two passes in a row (an interface coming up often goes
// through an address or two on the way), and the first signature is the starting point, not a change. After a change
// every RTSP session rides a connection the camera may no longer be able to answer on, so the keeper ends them cleanly
// (TEARDOWN, sockets closed) and connects again, instead of waiting for each to go quiet.
class r_network_watch
{
public:
    // True once, when the network has changed and stayed changed.
    bool poll(const std::string& signature)
    {
        if(!_started)
        {
            _started = true;
            _settled = signature;
            return false;
        }
        if(signature == _settled)
        {
            _has_pending = false;
            return false;
        }
        if(_has_pending && signature == _pending)
        {
            _settled = signature;
            _has_pending = false;
            return true;
        }
        _pending = signature;
        _has_pending = true;
        return false;
    }

private:
    bool _started {false};
    std::string _settled;
    bool _has_pending {false};
    std::string _pending;
};

}

#endif
