#include "framework.h"

class test_r_motion_events_query : public test_fixture
{
public:
    RTF_FIXTURE(test_r_motion_events_query);
      TEST(test_r_motion_events_query::test_window_before_the_ring_finds_its_events);
      TEST(test_r_motion_events_query::test_window_wholly_before_the_ring_is_empty);
      TEST(test_r_motion_events_query::test_oldest_is_creation_or_length_back);
    RTF_FIXTURE_END();

    virtual ~test_r_motion_events_query() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_window_before_the_ring_finds_its_events();
    void test_window_wholly_before_the_ring_is_empty();
    void test_oldest_is_creation_or_length_back();
};
