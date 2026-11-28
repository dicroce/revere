#include "framework.h"

class test_font_scale : public test_fixture
{
public:
    RTF_FIXTURE(test_font_scale);
      TEST(test_font_scale::test_text_measures_the_same_at_any_scale);
      TEST(test_font_scale::test_glyphs_have_the_displays_pixels);
      TEST(test_font_scale::test_scale_1_is_as_before);
      TEST(test_font_scale::test_the_display_scale);
    RTF_FIXTURE_END();

    virtual ~test_font_scale() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_text_measures_the_same_at_any_scale();
    void test_glyphs_have_the_displays_pixels();
    void test_scale_1_is_as_before();
    void test_the_display_scale();
};
