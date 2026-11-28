#include "framework.h"

class test_r_playback_restream : public test_fixture
{
public:
    RTF_FIXTURE(test_r_playback_restream);
      TEST(test_r_playback_restream::test_plays_with_no_camera_connected);
      TEST(test_r_playback_restream::test_plays_to_the_end_of_its_range);
      TEST(test_r_playback_restream::test_mount_goes_with_its_session);
      TEST(test_r_playback_restream::test_newer_mount_stays);
      TEST(test_r_playback_restream::test_unplayed_mount_frees_its_mount_points);
      TEST(test_r_playback_restream::test_unused_mount_expires);
      TEST(test_r_playback_restream::test_nothing_recorded_mounts_nothing);
      TEST(test_r_playback_restream::test_playback_camera);
    RTF_FIXTURE_END();

    virtual ~test_r_playback_restream() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_plays_with_no_camera_connected();
    void test_plays_to_the_end_of_its_range();
    void test_mount_goes_with_its_session();
    void test_newer_mount_stays();
    void test_unplayed_mount_frees_its_mount_points();
    void test_unused_mount_expires();
    void test_nothing_recorded_mounts_nothing();
    void test_playback_camera();
};
