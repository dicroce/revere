#include "test_r_motion_events_query.h"
#include "r_vss/r_query.h"
#include "r_storage/r_ring.h"
#include "r_utils/r_file.h"
#include <chrono>
#include <string>
#include <thread>

using namespace r_vss;
using namespace r_storage;
using namespace std;
using namespace std::chrono;

REGISTER_TEST_FIXTURE(test_r_motion_events_query);

static const string RING = "motion_events_query_ut.ring";

void test_r_motion_events_query::setup()
{
    if(r_utils::r_fs::file_exists(RING))
        r_utils::r_fs::remove_file(RING);
}

void test_r_motion_events_query::teardown()
{
    if(r_utils::r_fs::file_exists(RING))
        r_utils::r_fs::remove_file(RING);
}

// A 60-second ring with 3 s of motion written from now; returns when that motion began, once it's in the past.
static system_clock::time_point _ring_with_motion()
{
    r_ring::allocate(RING, 1, 60);
    r_ring r(RING, 1);
    auto began = system_clock::now();
    uint8_t one = 1;
    r.write_range(began, began + seconds(3), &one);
    this_thread::sleep_for(milliseconds(4500));
    return began;
}

void test_r_motion_events_query::test_window_before_the_ring_finds_its_events()
{
    // A window that starts an hour back, before the ring's 60 s and before it was created: what the ring holds is
    // read, so the event is found. It used to be "query start time is too old", and /motion_events was empty.
    auto began = _ring_with_motion();
    r_ring r(RING, 1);
    auto events = motion_events_in_ring(r, began - hours(1), system_clock::now());
    RTF_ASSERT_EQUAL((int)events.size(), 1);
    RTF_ASSERT(events[0].start == time_point_cast<seconds>(began));
    RTF_ASSERT(events[0].end >= time_point_cast<seconds>(began) + seconds(3));

    // A window inside what it holds finds the same event.
    auto inside = motion_events_in_ring(r, r.oldest(), system_clock::now());
    RTF_ASSERT_EQUAL((int)inside.size(), 1);
    RTF_ASSERT(inside[0].start == events[0].start);
}

void test_r_motion_events_query::test_window_wholly_before_the_ring_is_empty()
{
    auto began = _ring_with_motion();
    r_ring r(RING, 1);
    RTF_ASSERT(motion_events_in_ring(r, began - hours(2), began - hours(1)).empty());
}

void test_r_motion_events_query::test_oldest_is_creation_or_length_back()
{
    r_ring::allocate(RING, 1, 60);
    r_ring r(RING, 1);
    auto now = system_clock::now();
    auto oldest = r.oldest();
    // Created just now: nothing older than that, though 60 s back is inside its length.
    RTF_ASSERT(oldest >= time_point_cast<seconds>(now) - seconds(1));
    RTF_ASSERT(oldest <= now);
    RTF_ASSERT(oldest > now - seconds(60));
}
