
#include "framework.h"

class test_r_tier_outputs : public test_fixture
{
public:
    RTF_FIXTURE(test_r_tier_outputs);
      TEST(test_r_tier_outputs::test_camera_report_keeps_the_cameras_word);
      TEST(test_r_tier_outputs::test_camera_report_has_no_box);
      TEST(test_r_tier_outputs::test_analytics_json_leaves_out_a_reports_box);
      TEST(test_r_tier_outputs::test_plugins_get_a_report_with_no_box);
    RTF_FIXTURE_END();

    virtual ~test_r_tier_outputs() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_camera_report_keeps_the_cameras_word();
    void test_camera_report_has_no_box();
    void test_analytics_json_leaves_out_a_reports_box();
    void test_plugins_get_a_report_with_no_box();
};
