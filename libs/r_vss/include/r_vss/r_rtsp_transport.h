#ifndef __revere_rtsp_transport_h
#define __revere_rtsp_transport_h

#include "r_utils/r_macro.h"
#include <string>

namespace r_vss
{

// Which RTSP lower transport to record a camera over. TCP first: over UDP a
// high-resolution keyframe is hundreds of packets in one burst, and under load
// lost or reordered packets end up as corrupt or out-of-order frames in the
// recording. UDP only for a camera that won't stream over TCP.
//
// One instance per camera, kept across reconnects. Each recording attempt
// reports how far it got when it ends:
//   - video arrived: this transport works for the camera, keep it from now on;
//   - the camera answered (its SDP arrived) but no video came: the transport
//     is the problem, so until one has worked, switch to the other one;
//   - the camera didn't answer: it's unreachable, which says nothing about
//     transports, so stay on this one (an offline camera isn't moved to UDP).
class r_rtsp_transport final
{
public:
    // "TCP" or "UDP", for r_gst_source's "protocols" argument
    R_API std::string protocols() const;

    // Returns true when the next attempt will use the other transport.
    R_API bool attempt_ended(bool answered, bool received_video);

    R_API bool confirmed() const { return _confirmed; }

private:
    bool _udp {false};
    bool _confirmed {false};
};

}

#endif
