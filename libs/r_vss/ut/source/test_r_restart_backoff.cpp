#include "test_r_restart_backoff.h"
#include "r_vss/r_restart_backoff.h"

using namespace std;
using namespace std::chrono;
using namespace r_vss;

REGISTER_TEST_FIXTURE(test_r_restart_backoff);

void test_r_restart_backoff::test_new_camera_starts_at_once()
{
    r_restart_backoff b;
    RTF_ASSERT(b.may_start(r_restart_backoff::clock::now()));
    RTF_ASSERT(b.failures() == 0 && b.wait() == seconds(0));
}

void test_r_restart_backoff::test_stream_that_had_video_restarts_at_once()
{
    // A camera that was recording comes back as soon as it can, every time.
    r_restart_backoff b;
    auto t = r_restart_backoff::clock::now();
    for(int i = 0; i < 5; ++i)
    {
        b.ended(true, t);
        RTF_ASSERT(b.may_start(t));
        RTF_ASSERT(b.wait() == seconds(0));
    }
}

void test_r_restart_backoff::test_no_video_waits_longer_each_time()
{
    // TCP connects but nothing comes (a camera holding sessions it was never told to end): 20 s, 60 s, then every
    // 2 minutes, rather than a new connection every 20 seconds.
    r_restart_backoff b;
    auto t = r_restart_backoff::clock::now();
    const seconds expected[] = {seconds(20), seconds(60), seconds(120), seconds(120), seconds(120)};
    for(auto w : expected)
    {
        b.ended(false, t);
        RTF_ASSERT(b.wait() == w);
        RTF_ASSERT(!b.may_start(t));
        RTF_ASSERT(!b.may_start(t + w - milliseconds(1)));
        RTF_ASSERT(b.may_start(t + w));
        t += w;
    }
    RTF_ASSERT(b.failures() == 5);
}

void test_r_restart_backoff::test_first_frame_resets_the_wait()
{
    r_restart_backoff b;
    auto t = r_restart_backoff::clock::now();
    b.ended(false, t);
    b.ended(false, t);
    RTF_ASSERT(b.wait() == seconds(60));
    b.ended(true, t);
    RTF_ASSERT(b.may_start(t) && b.failures() == 0);
    b.ended(false, t);
    RTF_ASSERT(b.wait() == seconds(20));
}
