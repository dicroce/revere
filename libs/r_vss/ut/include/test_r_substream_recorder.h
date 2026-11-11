#include "framework.h"

class test_r_substream_recorder : public test_fixture
{
public:
    RTF_FIXTURE(test_r_substream_recorder);
      TEST(test_r_substream_recorder::test_ring_path);
      TEST(test_r_substream_recorder::test_next_step);
      TEST(test_r_substream_recorder::test_off_for_the_computer_means_not_in_use);
      TEST(test_r_substream_recorder::test_records_into_its_ring);
      TEST(test_r_substream_recorder::test_no_camera);
      TEST(test_r_substream_recorder::test_frames_go_on_at_the_time_the_ring_stored_them);
      TEST(test_r_substream_recorder::test_posts_to_the_frame_feeds);
      TEST(test_r_substream_recorder::test_mount_path);
      TEST(test_r_substream_recorder::test_view_mount_path);
      TEST(test_r_substream_recorder::test_serves_its_sub_mount);
      TEST(test_r_substream_recorder::test_snapshot_fits);
      TEST(test_r_substream_recorder::test_keeps_its_newest_key_frame_for_snapshots);
      TEST(test_r_substream_recorder::test_key_interval);
      TEST(test_r_substream_recorder::test_motion_on_sub_possible);
      TEST(test_r_substream_recorder::test_motion_source_governor);
      TEST(test_r_substream_recorder::test_feeds_motion_key_frames_only_when_told);
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
    void test_off_for_the_computer_means_not_in_use();
    void test_records_into_its_ring();
    void test_no_camera();
    void test_frames_go_on_at_the_time_the_ring_stored_them();
    void test_posts_to_the_frame_feeds();
    void test_mount_path();
    void test_view_mount_path();
    void test_serves_its_sub_mount();
    void test_snapshot_fits();
    void test_keeps_its_newest_key_frame_for_snapshots();
    void test_key_interval();
    void test_motion_on_sub_possible();
    void test_motion_source_governor();
    void test_feeds_motion_key_frames_only_when_told();
    void test_governor_starts_after_main_and_backs_off();
    void test_governor_counts_a_refusal_at_setup();
    void test_governor_holds_a_given_up_camera_until_its_context_changes();
    void test_governor_counts_main_drops_soon_after_a_start();
    void test_governor_stops_a_moved_ring();
    void test_governor_forget();
};
