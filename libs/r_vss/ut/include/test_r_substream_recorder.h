#include "framework.h"

class test_r_substream_recorder : public test_fixture
{
public:
    RTF_FIXTURE(test_r_substream_recorder);
      TEST(test_r_substream_recorder::test_ring_path);
      TEST(test_r_substream_recorder::test_next_step);
      TEST(test_r_substream_recorder::test_records_into_its_ring);
      TEST(test_r_substream_recorder::test_no_camera);
      TEST(test_r_substream_recorder::test_governor_starts_after_main_and_backs_off);
      TEST(test_r_substream_recorder::test_governor_counts_a_refusal_at_setup);
      TEST(test_r_substream_recorder::test_governor_holds_a_given_up_camera_until_its_context_changes);
      TEST(test_r_substream_recorder::test_governor_counts_main_drops_soon_after_a_start);
      TEST(test_r_substream_recorder::test_governor_stops_a_moved_ring);
      TEST(test_r_substream_recorder::test_governor_forget);
    RTF_FIXTURE_END();

    virtual ~test_r_substream_recorder() throw() {}
    virtual void setup();
    virtual void teardown();

    void test_ring_path();
    void test_next_step();
    void test_records_into_its_ring();
    void test_no_camera();
    void test_governor_starts_after_main_and_backs_off();
    void test_governor_counts_a_refusal_at_setup();
    void test_governor_holds_a_given_up_camera_until_its_context_changes();
    void test_governor_counts_main_drops_soon_after_a_start();
    void test_governor_stops_a_moved_ring();
    void test_governor_forget();
};
