#include "framework.h"
#include <string>

class test_r_system_plugin_frames : public test_fixture
{
public:
    RTF_FIXTURE(test_r_system_plugin_frames);
      TEST(test_r_system_plugin_frames::test_a_plugin_gets_the_host_functions);
      TEST(test_r_system_plugin_frames::test_a_plugin_reads_a_cameras_frames);
      TEST(test_r_system_plugin_frames::test_an_older_r_frame_is_not_overrun);
      TEST(test_r_system_plugin_frames::test_unknown_cameras_streams_and_media);
      TEST(test_r_system_plugin_frames::test_stopping_closes_what_a_plugin_left);
      TEST(test_r_system_plugin_frames::test_without_a_frame_source_no_functions);
      TEST(test_r_system_plugin_frames::test_a_small_bound_still_fits_a_key_frame);
      TEST(test_r_system_plugin_frames::test_at_most_16_feeds_a_plugin);
    RTF_FIXTURE_END();

    virtual ~test_r_system_plugin_frames() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_a_plugin_gets_the_host_functions();
    void test_a_plugin_reads_a_cameras_frames();
    void test_an_older_r_frame_is_not_overrun();
    void test_unknown_cameras_streams_and_media();
    void test_stopping_closes_what_a_plugin_left();
    void test_without_a_frame_source_no_functions();
    void test_a_small_bound_still_fits_a_key_frame();
    void test_at_most_16_feeds_a_plugin();

private:
    std::string _top;
};
