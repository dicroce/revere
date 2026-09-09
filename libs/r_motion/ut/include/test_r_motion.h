#include "framework.h"

class test_r_motion : public test_fixture
{
public:
    RTF_FIXTURE(test_r_motion);
      TEST(test_r_motion::test_motion_state);
      TEST(test_r_motion::test_adaptive_masking);
      TEST(test_r_motion::test_shadow_evidence);
      TEST(test_r_motion::test_illumination_and_large_object);
      TEST(test_r_motion::test_image_formats);
    RTF_FIXTURE_END();

    virtual ~test_r_motion() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_motion_state();
    void test_adaptive_masking();
    void test_shadow_evidence();
    void test_illumination_and_large_object();
    void test_image_formats();
};
