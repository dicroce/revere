#include "r_vss/r_rtsp_transport.h"

using namespace r_vss;
using namespace std;

string r_rtsp_transport::protocols() const
{
    return _udp ? "UDP" : "TCP";
}

bool r_rtsp_transport::attempt_ended(bool answered, bool received_video)
{
    if(received_video)
    {
        _confirmed = true;
        return false;
    }

    if(!answered || _confirmed)
        return false;

    _udp = !_udp;
    return true;
}
