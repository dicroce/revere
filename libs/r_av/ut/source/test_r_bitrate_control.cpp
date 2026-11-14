#include "test_r_bitrate_control.h"
#include "r_av/r_bitrate_control.h"

using namespace r_av;

REGISTER_TEST_FIXTURE(test_r_bitrate_control);

// Bytes produced in a 5 s window at this many bits per second.
static size_t _bytes_at(uint32_t bps) { return (size_t)bps / 8 * 5; }
static const uint32_t W = 5000; // ms

void test_r_bitrate_control::test_the_first_window_is_not_judged()
{
    r_bitrate_control c(2000000, 500000, 4000000);
    // The key-frame burst: far over the target. Not judged; the target stays.
    RTF_ASSERT(!c.review(_bytes_at(3100000), W));
    RTF_ASSERT(c.current() == 2000000);
    // The next window is: the same overshoot now cuts.
    auto n = c.review(_bytes_at(3100000), W);
    RTF_ASSERT(n.has_value());
    RTF_ASSERT(*n == 1400000);
    RTF_ASSERT(c.current() == 1400000);
}

void test_r_bitrate_control::test_an_overshoot_cuts_the_target_by_30_percent_to_the_floor()
{
    r_bitrate_control c(2000000, 500000, 4000000);
    c.review(0, W); // the skipped window
    uint32_t last = c.current();
    const uint32_t steps[] = {1400000, 980000, 686000, 500000};
    for(uint32_t want : steps)
    {
        auto n = c.review(_bytes_at(last * 2), W);
        RTF_ASSERT(n.has_value());
        RTF_ASSERT(*n == want);
        last = *n;
    }
    // At the floor there is nothing more to cut.
    RTF_ASSERT(!c.review(_bytes_at(3000000), W));
    RTF_ASSERT(c.current() == 500000);
}

void test_r_bitrate_control::test_an_undershoot_raises_the_target_by_25_percent_to_the_cap()
{
    r_bitrate_control c(2000000, 500000, 4000000);
    c.review(0, W);
    auto a = c.review(_bytes_at(500000), W);
    RTF_ASSERT(a && *a == 2500000);
    auto b = c.review(_bytes_at(500000), W);
    RTF_ASSERT(b && *b == 3125000);
    auto d = c.review(_bytes_at(500000), W);
    RTF_ASSERT(d && *d == 3906250);
    // The next step would be to the 4 Mbps cap, but that is under 10% of the target: not worth a change.
    RTF_ASSERT(!c.review(_bytes_at(500000), W));
    RTF_ASSERT(c.current() == 3906250);
}

void test_r_bitrate_control::test_a_target_near_what_is_produced_is_left_alone()
{
    r_bitrate_control c(2000000, 500000, 4000000);
    c.review(0, W);
    RTF_ASSERT(!c.review(_bytes_at(2000000), W));
    RTF_ASSERT(!c.review(_bytes_at(2200000), W));   // 1.1 x
    RTF_ASSERT(!c.review(_bytes_at(1800000), W));   // 0.9 x
    RTF_ASSERT(c.current() == 2000000);
    RTF_ASSERT(!c.review(0, 0));                    // no window length: nothing to judge

    // Just past the edges: 1.2 x cuts, 0.8 x raises.
    r_bitrate_control over(2000000, 500000, 4000000);
    over.review(0, W);
    RTF_ASSERT(over.review(_bytes_at(2400000), W).value_or(0) == 1400000);
    r_bitrate_control under(2000000, 500000, 4000000);
    under.review(0, W);
    RTF_ASSERT(under.review(_bytes_at(1600000), W).value_or(0) == 2500000);
}

void test_r_bitrate_control::test_a_restart_skips_its_first_window_and_keeps_the_target()
{
    r_bitrate_control c(2000000, 500000, 4000000);
    c.review(0, W);
    RTF_ASSERT(c.review(_bytes_at(4000000), W).has_value());
    RTF_ASSERT(c.current() == 1400000);
    c.restart();
    RTF_ASSERT(!c.review(_bytes_at(4000000), W));   // skipped again
    RTF_ASSERT(c.current() == 1400000);             // the target is where it was
    RTF_ASSERT(c.review(_bytes_at(4000000), W).has_value());
}

// The window is checked on a frame, so it runs over 5 s. The same bytes over what really elapsed are a lower rate:
// 2.3 Mbps measured over 5 s reads 1.15 x a 2 Mbps target (a cut), but those bytes were 6 s of 1.92 Mbps (not a cut).
void test_r_bitrate_control::test_the_rate_is_over_the_time_that_elapsed()
{
    RTF_ASSERT(r_bitrate_control::bits_per_second(1000000, 5000) == 1600000);
    RTF_ASSERT(r_bitrate_control::bits_per_second(1000000, 6000) == 1333333);
    RTF_ASSERT(r_bitrate_control::bits_per_second(5, 0) == 0);

    r_bitrate_control c(2000000, 500000, 4000000);
    c.review(0, 5000);
    const size_t bytes = 6 * 1920000 / 8;                 // 6 s of 1.92 Mbps
    RTF_ASSERT(!c.review(bytes, 6000));                   // in band: left alone
    RTF_ASSERT(c.current() == 2000000);

    r_bitrate_control fixed(2000000, 500000, 4000000);
    fixed.review(0, 5000);
    RTF_ASSERT(fixed.review(bytes, 5000).has_value());    // the same bytes taken as 5 s would have cut
}
