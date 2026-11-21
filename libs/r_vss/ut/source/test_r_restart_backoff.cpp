#include "test_r_restart_backoff.h"
#include "r_vss/r_restart_backoff.h"

using namespace std;
using namespace std::chrono;
using namespace r_vss;

REGISTER_TEST_FIXTURE(test_r_restart_backoff);

static const milliseconds NONE {0};
static const milliseconds LONG {duration_cast<milliseconds>(r_restart_backoff::LONG_SESSION)};

void test_r_restart_backoff::test_new_camera_starts_at_once()
{
    r_restart_backoff b;
    RTF_ASSERT(b.may_start(r_restart_backoff::clock::now(), "h1"));
    RTF_ASSERT(b.failures() == 0 && b.wait() == seconds(0));
}

void test_r_restart_backoff::test_stream_that_had_video_restarts_at_once()
{
    // A camera that was recording comes back as soon as it can, every time.
    r_restart_backoff b;
    auto t = r_restart_backoff::clock::now();
    for(int i = 0; i < 5; ++i)
    {
        b.ended(true, true, LONG, t, "h1");
        RTF_ASSERT(b.may_start(t, "h1"));
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
        b.ended(false, true, NONE, t, "h1");
        RTF_ASSERT(b.wait() == w);
        RTF_ASSERT(!b.may_start(t, "h1"));
        RTF_ASSERT(!b.may_start(t + w - milliseconds(1), "h1"));
        RTF_ASSERT(b.may_start(t + w, "h1"));
        t += w;
    }
    RTF_ASSERT(b.failures() == 5);
}

void test_r_restart_backoff::test_first_frame_resets_the_wait()
{
    r_restart_backoff b;
    auto t = r_restart_backoff::clock::now();
    b.ended(false, true, NONE, t, "h1");
    b.ended(false, true, NONE, t, "h1");
    RTF_ASSERT(b.wait() == seconds(60));
    b.ended(true, true, LONG, t, "h1");
    RTF_ASSERT(b.may_start(t, "h1") && b.failures() == 0);
    b.ended(false, true, NONE, t, "h1");
    RTF_ASSERT(b.wait() == seconds(20));
}

void test_r_restart_backoff::test_new_settings_start_at_once()
{
    // The camera's stream settings changed (a fixed URL): it starts at once, and its count starts over.
    r_restart_backoff b;
    auto t = r_restart_backoff::clock::now();
    b.ended(false, true, NONE, t, "h1");
    b.ended(false, true, NONE, t, "h1");
    RTF_ASSERT(!b.may_start(t, "h1"));
    RTF_ASSERT(b.may_start(t, "h2"));
    b.ended(false, true, NONE, t, "h2");
    RTF_ASSERT(b.failures() == 1 && b.wait() == seconds(20));
    RTF_ASSERT(!b.may_start(t, "h2"));
}

void test_r_restart_backoff::test_no_answer_retries_quickly_at_first()
{
    // The camera didn't answer (its RTSP server stopped, it was unplugged): 1 s, 2 s, 4 s, then every 5 s, so one back
    // from a short outage records again within seconds; after 2 minutes of that, every 2 minutes.
    r_restart_backoff b;
    auto t = r_restart_backoff::clock::now();
    const seconds quick[] = {seconds(1), seconds(2), seconds(4), seconds(5), seconds(5)};
    auto began = t;
    for(auto w : quick)
    {
        b.ended(false, false, NONE, t, "h1");
        RTF_ASSERT(b.wait() == w);
        RTF_ASSERT(!b.may_start(t + w - milliseconds(1), "h1"));
        RTF_ASSERT(b.may_start(t + w, "h1"));
        t += w;
    }
    while(t - began < minutes(2))
    {
        b.ended(false, false, NONE, t, "h1");
        RTF_ASSERT(b.wait() == seconds(5));
        t += b.wait();
    }
    b.ended(false, false, NONE, t, "h1");
    RTF_ASSERT(b.wait() == seconds(120));

    // Video again resets it: the next outage starts quick again.
    b.ended(true, true, LONG, t, "h1");
    b.ended(false, false, NONE, t, "h1");
    RTF_ASSERT(b.wait() == seconds(1));

    // Answered but no video (a stuck session) keeps the long waits.
    r_restart_backoff s;
    s.ended(false, true, NONE, t, "h1");
    RTF_ASSERT(s.wait() == seconds(20));
}

void test_r_restart_backoff::test_short_sessions_wait_longer_each_time()
{
    // A camera that sends a few seconds of video and hangs up, over and over: the first restart is at once, then
    // 2 s, 4 s, 8 s, 16 s, 32 s, then every minute, rather than a new connection every couple of seconds.
    r_restart_backoff b;
    auto t = r_restart_backoff::clock::now();
    const seconds expected[] = {seconds(0), seconds(2), seconds(4), seconds(8), seconds(16), seconds(32), seconds(60), seconds(60), seconds(60)};
    int n = 0;
    for(auto w : expected)
    {
        b.ended(true, true, seconds(3), t, "h1");
        RTF_ASSERT(b.wait() == w);
        RTF_ASSERT_EQUAL(b.short_sessions(), ++n);
        RTF_ASSERT_EQUAL(b.failures(), 0);
        if(w.count() > 0)
            RTF_ASSERT(!b.may_start(t + w - milliseconds(1), "h1"));
        RTF_ASSERT(b.may_start(t + w, "h1"));
        t += w;
    }

    // Just under a minute is still short.
    b.ended(true, true, r_restart_backoff::LONG_SESSION - milliseconds(1), t, "h1");
    RTF_ASSERT(b.wait() == seconds(60));

    // A session that streams for a minute resets it: back at once, and the next short one is at once too.
    b.ended(true, true, r_restart_backoff::LONG_SESSION, t, "h1");
    RTF_ASSERT(b.wait() == seconds(0) && b.short_sessions() == 0 && b.may_start(t, "h1"));
    b.ended(true, true, seconds(3), t, "h1");
    RTF_ASSERT(b.wait() == seconds(0));
    b.ended(true, true, seconds(3), t, "h1");
    RTF_ASSERT(b.wait() == seconds(2));
}

void test_r_restart_backoff::test_short_sessions_and_failures_mix()
{
    // An attempt with no video in between doesn't reset the short-session count, and a short session ends a run of
    // failures (it did stream).
    r_restart_backoff b;
    auto t = r_restart_backoff::clock::now();
    b.ended(true, true, seconds(3), t, "h1");
    b.ended(true, true, seconds(3), t, "h1");
    RTF_ASSERT(b.wait() == seconds(2));
    b.ended(false, true, NONE, t, "h1");
    RTF_ASSERT(b.wait() == seconds(20) && b.failures() == 1 && b.short_sessions() == 2);
    b.ended(true, true, seconds(3), t, "h1");
    RTF_ASSERT(b.wait() == seconds(4) && b.failures() == 0 && b.short_sessions() == 3);

    // New stream settings start the count over.
    b.ended(true, true, seconds(3), t, "h2");
    RTF_ASSERT(b.wait() == seconds(0) && b.short_sessions() == 1);
}
