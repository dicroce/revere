#include "framework.h"

class test_r_camera_stats : public test_fixture
{
public:
    RTF_FIXTURE(test_r_camera_stats);
      TEST(test_r_camera_stats::test_motion_hour_buckets);
      TEST(test_r_camera_stats::test_count_detections);
      TEST(test_r_camera_stats::test_covered_time);
      TEST(test_r_camera_stats::test_camera_storage_files);
      TEST(test_r_camera_stats::test_reconnect_log);
      TEST(test_r_camera_stats::test_local_day_start);
      TEST(test_r_camera_stats::test_formats);
      TEST(test_r_camera_stats::test_stats_cache);
      TEST(test_r_camera_stats::test_stats_cache_one_at_a_time);
    RTF_FIXTURE_END();

    virtual ~test_r_camera_stats() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_motion_hour_buckets();
    void test_count_detections();
    void test_covered_time();
    void test_camera_storage_files();
    void test_reconnect_log();
    void test_local_day_start();
    void test_formats();
    void test_stats_cache();
    void test_stats_cache_one_at_a_time();
};
