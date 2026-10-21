
#include "test_r_rtsp_transport.h"
#include "r_vss/r_rtsp_transport.h"

using namespace std;
using namespace r_vss;

REGISTER_TEST_FIXTURE(test_r_rtsp_transport);

void test_r_rtsp_transport::test_starts_on_tcp()
{
    r_rtsp_transport t;
    RTF_ASSERT(t.protocols() == "TCP");
    RTF_ASSERT(!t.confirmed());
}

void test_r_rtsp_transport::test_unreachable_camera_stays_on_tcp()
{
    // A camera that's off at startup never answers. That says nothing about
    // transports, so it must not end up on UDP when it comes back.
    r_rtsp_transport t;
    for(int i = 0; i < 10; ++i)
        RTF_ASSERT(!t.attempt_ended(false, false));
    RTF_ASSERT(t.protocols() == "TCP");
}

void test_r_rtsp_transport::test_tcp_refused_falls_back_to_udp()
{
    // The camera answers (DESCRIBE works) but sends no video over TCP
    r_rtsp_transport t;
    RTF_ASSERT(t.attempt_ended(true, false));
    RTF_ASSERT(t.protocols() == "UDP");

    // UDP works, and from then on the camera stays on it
    RTF_ASSERT(!t.attempt_ended(true, true));
    RTF_ASSERT(t.confirmed());
    RTF_ASSERT(!t.attempt_ended(true, false));
    RTF_ASSERT(!t.attempt_ended(false, false));
    RTF_ASSERT(t.protocols() == "UDP");
}

void test_r_rtsp_transport::test_working_transport_is_kept()
{
    // Once TCP has delivered video, a later attempt that answers but stalls
    // (a camera rebooting mid-setup, say) doesn't move it to UDP
    r_rtsp_transport t;
    RTF_ASSERT(!t.attempt_ended(true, true));
    RTF_ASSERT(!t.attempt_ended(true, false));
    RTF_ASSERT(!t.attempt_ended(true, false));
    RTF_ASSERT(t.protocols() == "TCP");
}

void test_r_rtsp_transport::test_neither_working_alternates()
{
    // Until one transport has worked, answered-but-no-video keeps trying the
    // other, so a camera that briefly failed over TCP gets TCP again
    r_rtsp_transport t;
    RTF_ASSERT(t.attempt_ended(true, false));
    RTF_ASSERT(t.protocols() == "UDP");
    RTF_ASSERT(t.attempt_ended(true, false));
    RTF_ASSERT(t.protocols() == "TCP");
    RTF_ASSERT(!t.attempt_ended(true, true));
    RTF_ASSERT(t.protocols() == "TCP");
}
