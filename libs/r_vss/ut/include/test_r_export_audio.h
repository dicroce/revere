#include "framework.h"

class test_r_export_audio : public test_fixture
{
public:
    RTF_FIXTURE(test_r_export_audio);
      TEST(test_r_export_audio::test_container_holds);
      TEST(test_r_export_audio::test_g711_in_mp4_becomes_aac);
      TEST(test_r_export_audio::test_copied_where_the_container_holds_it);
      TEST(test_r_export_audio::test_dropped_with_why);
      TEST(test_r_export_audio::test_audio_after_a_gap_keeps_its_time);
    RTF_FIXTURE_END();

    virtual ~test_r_export_audio() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_container_holds();
    void test_g711_in_mp4_becomes_aac();
    void test_copied_where_the_container_holds_it();
    void test_dropped_with_why();
    void test_audio_after_a_gap_keeps_its_time();
};
