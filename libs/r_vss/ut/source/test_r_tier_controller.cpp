#include "test_r_tier_controller.h"
#include "r_vss/r_tier_controller.h"
#include <string>

using namespace r_vss;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_tier_controller);

typedef r_tier_controller T;

static const int64_t MIN = 60 * 1000;

// One motion event at t (10 s long): MOG2 saw it; the camera reported motion
// if `camera`, YOLO found a person if `person`, the camera said person if
// `camera_person`.
static void _event(T& t, const string& id, int64_t at, bool camera, bool person = false, bool camera_person = false)
{
    if (camera)
    {
        t.camera_motion(id, true, at + 500);
        t.camera_motion(id, false, at + 9000);
    }
    if (camera_person)
    {
        t.camera_class(id, "person", true, at + 1500);
        t.camera_class(id, "person", false, at + 8000);
    }
    t.mog2_event(id, at, at + 10000);
    if (person)
        t.yolo_detection(id, "person", at + 4000);
}

// n events a minute apart from `from`, all matching, then ticks to settle.
static int64_t _events(T& t, const string& id, int64_t from, int n, bool person = false, bool camera_person = false)
{
    int64_t at = from;
    for (int i = 0; i < n; ++i, at += MIN)
    {
        _event(t, id, at, true, person, camera_person);
        t.tick(at + 10000);
    }
    t.tick(at + T::VERIFY_DELAY_MS);
    return at + T::VERIFY_DELAY_MS;
}

void test_r_tier_controller::test_no_subscription_stays_tier_1()
{
    T t;
    _events(t, "a", 10 * MIN, 10);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);
    RTF_ASSERT_EQUAL(t.tier("unknown"), (int)motion_tier_1);
}

void test_r_tier_controller::test_matching_motion_moves_to_tier_3()
{
    T t;
    t.subscription("a", true, 0);
    t.tick(0);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);

    auto now = _events(t, "a", 10 * MIN, (int)T::MIN_EVENTS - 1);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);  // one short

    now = _events(t, "a", now + MIN, 1);
    auto changed = t.tick(now);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_3);
    auto st = t.status("a");
    RTF_ASSERT_EQUAL(st.checked, (int)T::MIN_EVENTS);
    RTF_ASSERT_EQUAL(st.matched, (int)T::MIN_EVENTS);
    RTF_ASSERT(st.automatic);
}

void test_r_tier_controller::test_matching_person_moves_to_tier_2()
{
    T t;
    t.subscription("a", true, 0);
    _events(t, "a", 10 * MIN, 4);
    _events(t, "a", 20 * MIN, 4, true, true);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_2);
    RTF_ASSERT_EQUAL(t.status("a").person_checked, 4);
    RTF_ASSERT_EQUAL(t.status("a").person_matched, 4);

    // People YOLO found but the camera didn't report: tier 3.
    T u;
    u.subscription("b", true, 0);
    _events(u, "b", 10 * MIN, 4);
    _events(u, "b", 20 * MIN, 4, true, false);
    RTF_ASSERT_EQUAL(u.tier("b"), (int)motion_tier_3);
}

void test_r_tier_controller::test_missed_events_stay_tier_1()
{
    T t;
    t.subscription("a", true, 0);
    int64_t at = 10 * MIN;
    for (int i = 0; i < 10; ++i, at += MIN)
    {
        _event(t, "a", at, i % 3 != 0);   // misses 4 of 10
        t.tick(at + 10000);
    }
    t.tick(at + T::VERIFY_DELAY_MS);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);
    auto st = t.status("a");
    RTF_ASSERT_EQUAL(st.checked, 10);
    RTF_ASSERT_EQUAL(st.matched, 6);
    RTF_ASSERT(st.reason.find("detection zones") != string::npos);
}

void test_r_tier_controller::test_waits_for_stable_subscription()
{
    T t;
    t.subscription("a", true, 0);
    auto now = _events(t, "a", 10 * MIN, (int)T::MIN_EVENTS);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_3);

    // Down briefly (less than DOWN_MS): stays; then back up.
    t.subscription("a", false, now);
    t.tick(now + T::DOWN_MS - 1);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_3);
    t.subscription("a", true, now + T::DOWN_MS - 1);
    t.tick(now + T::DOWN_MS);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_3);
}

void test_r_tier_controller::test_down_falls_back_and_returns()
{
    T t;
    t.subscription("a", true, 0);
    auto now = _events(t, "a", 10 * MIN, (int)T::MIN_EVENTS);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_3);

    t.subscription("a", false, now);
    auto changed = t.tick(now + T::DOWN_MS);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);
    RTF_ASSERT_EQUAL(changed.size(), (size_t)1);

    // Back up: tier 1 until it has stayed up for STABLE_MS, then straight back
    // (the camera's events were already verified).
    int64_t up_at = now + 10 * MIN;
    t.subscription("a", true, up_at);
    t.tick(up_at + T::STABLE_MS - 1);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);
    t.camera_motion("a", true, up_at + 1000);  // not silent
    t.tick(up_at + T::STABLE_MS);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_3);
}

void test_r_tier_controller::test_silence_checks_again()
{
    T t;
    t.subscription("a", true, 0);
    auto now = _events(t, "a", 10 * MIN, (int)T::MIN_EVENTS);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_3);

    t.tick(now + T::SILENCE_MS - 1);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_3);
    t.tick(now + T::SILENCE_MS + T::VERIFY_DELAY_MS);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);
    RTF_ASSERT_EQUAL(t.status("a").checked, 0);  // from scratch
}

void test_r_tier_controller::test_outage_events_not_counted()
{
    T t;
    t.subscription("a", false, 0);
    int64_t at = 10 * MIN;
    for (int i = 0; i < 10; ++i, at += MIN)
    {
        _event(t, "a", at, false);
        t.tick(at + 10000);
    }
    t.tick(at + T::VERIFY_DELAY_MS);
    RTF_ASSERT_EQUAL(t.status("a").checked, 0);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);
}

void test_r_tier_controller::test_forced_tiers()
{
    T t;
    t.set_setting("a", "2");
    t.tick(0);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);  // no subscription yet

    t.subscription("a", true, 1000);
    t.tick(1000);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_2);  // no verification needed
    RTF_ASSERT(!t.status("a").automatic);

    t.subscription("a", false, 2000);
    t.tick(2000 + T::DOWN_MS);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);  // down: MOG2 takes over

    t.set_setting("a", "1");
    t.subscription("a", true, 3 * MIN);
    t.tick(3 * MIN);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);

    t.set_setting("a", "3");
    t.tick(4 * MIN);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_3);

    t.set_setting("a", "auto");
    t.tick(5 * MIN);
    RTF_ASSERT_EQUAL(t.tier("a"), (int)motion_tier_1);  // automatic: not verified yet
}
