#include "test_r_press_check.h"
#include "r_vss/r_press_check.h"

using namespace r_vss;
using namespace r_disco;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_press_check);

static const r_press_test_source& of(const r_press_test& t, const string& source)
{
    for (auto& s : t.sources)
        if (s.source == source)
            return s;
    throw runtime_error("no source " + source);
}

void test_r_press_check::test_a_test_notes_each_source_and_its_delay()
{
    // "ONVIF: no. Email: yes, 14 s after."
    r_press_check pc;
    pc.start("door", 100000, false, {"onvif", "reolink_email"});
    pc.press("door", "reolink_email", 105000, 119000);   // pressed at 105 s, its mail in 14 s later
    auto t = pc.test("door", 120000);
    RTF_ASSERT(t.any && t.open);
    RTF_ASSERT(!of(t, "onvif").seen);
    RTF_ASSERT(of(t, "reolink_email").seen);
    RTF_ASSERT_EQUAL(of(t, "reolink_email").press_ms, (int64_t)105000);
    RTF_ASSERT_EQUAL(of(t, "reolink_email").arrived_ms - of(t, "reolink_email").press_ms, (int64_t)14000);
    // A second report from the same source doesn't move the first.
    pc.press("door", "reolink_email", 130000, 131000);
    RTF_ASSERT_EQUAL(of(pc.test("door", 132000), "reolink_email").press_ms, (int64_t)105000);
    // Another doorbell has no test.
    RTF_ASSERT(!pc.test("side", 120000).any);
    RTF_ASSERT(!pc.press("side", "onvif", 120000, 120000));
}

void test_r_press_check::test_a_test_press_is_held_from_phones()
{
    // The person pressing is the one testing: their phones aren't rung, unless they asked.
    r_press_check pc;
    pc.start("door", 0, false, {"onvif"});
    RTF_ASSERT(pc.press("door", "onvif", 1000, 1000));
    pc.start("loud", 0, true, {"onvif"});
    RTF_ASSERT(!pc.press("loud", "onvif", 1000, 1000));
    RTF_ASSERT(of(pc.test("loud", 2000), "onvif").seen);
    // Once over, presses ring as ever.
    RTF_ASSERT(!pc.press("door", "onvif", 61000, 61000));
}

void test_r_press_check::test_a_test_closes_after_a_minute_and_keeps_its_result()
{
    r_press_check pc;
    auto started = pc.start("door", 10000, false, {"onvif", "reolink_email"});
    RTF_ASSERT_EQUAL(started.ends_ms, (int64_t)(10000 + r_press_check::TEST_MS));
    pc.press("door", "onvif", 20000, 20000);
    RTF_ASSERT(pc.test("door", 69999).open);
    auto over = pc.test("door", 70000);
    RTF_ASSERT(!over.open);
    RTF_ASSERT(over.any);
    RTF_ASSERT(of(over, "onvif").seen);              // kept, for setup and support
    RTF_ASSERT(!of(over, "reolink_email").seen);
    // A late mail after it closed isn't noted.
    pc.press("door", "reolink_email", 25000, 75000);
    RTF_ASSERT(!of(pc.test("door", 76000), "reolink_email").seen);
    // Started again: fresh.
    pc.start("door", 80000, false, {"onvif"});
    RTF_ASSERT(!of(pc.test("door", 80001), "onvif").seen);
    pc.forget("door");
    RTF_ASSERT(!pc.test("door", 80002).any);
}

void test_r_press_check::test_a_press_from_before_the_test_isnt_its()
{
    // An email for a press made before the test, arriving during it: not the test's press, and it rings.
    r_press_check pc;
    pc.start("door", 100000, false, {"onvif", "reolink_email"});
    RTF_ASSERT(!pc.press("door", "reolink_email", 95000, 109000));
    RTF_ASSERT(!of(pc.test("door", 110000), "reolink_email").seen);
}

void test_r_press_check::test_the_sources_a_test_watches()
{
    r_camera door;
    door.kind = string("doorbell");
    RTF_ASSERT(press_test_sources(door) == vector<string>({"onvif"}));
    door.press_source = string("reolink_email");
    RTF_ASSERT(press_test_sources(door) == vector<string>({"onvif", "reolink_email"}));
}

void test_r_press_check::test_suggest_onvif()
{
    // Set to email, and ONVIF has delivered a press: suggest it, for instant rings.
    r_camera door;
    door.kind = string("doorbell");
    door.press_source = string("reolink_email");
    RTF_ASSERT(!suggest_onvif(door, r_nullable<int64_t>()));
    RTF_ASSERT(suggest_onvif(door, r_nullable<int64_t>(5000)));
    door.press_source = string("onvif");
    RTF_ASSERT(!suggest_onvif(door, r_nullable<int64_t>(5000)));     // already on ONVIF
    door.press_source = string("reolink_email");
    door.kind = string("camera");
    RTF_ASSERT(!suggest_onvif(door, r_nullable<int64_t>(5000)));     // not a doorbell
}
