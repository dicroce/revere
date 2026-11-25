#include "framework.h"

// r_press_check: which of a doorbell's sources report its presses ("Press the doorbell now", and the ONVIF suggestion).
class test_r_press_check : public test_fixture
{
public:
    RTF_FIXTURE(test_r_press_check);
      TEST(test_r_press_check::test_a_test_notes_each_source_and_its_delay);
      TEST(test_r_press_check::test_a_test_press_is_held_from_phones);
      TEST(test_r_press_check::test_a_test_closes_after_a_minute_and_keeps_its_result);
      TEST(test_r_press_check::test_a_press_from_before_the_test_isnt_its);
      TEST(test_r_press_check::test_the_sources_a_test_watches);
      TEST(test_r_press_check::test_suggest_onvif);
    RTF_FIXTURE_END();

    virtual ~test_r_press_check() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_a_test_notes_each_source_and_its_delay();
    void test_a_test_press_is_held_from_phones();
    void test_a_test_closes_after_a_minute_and_keeps_its_result();
    void test_a_press_from_before_the_test_isnt_its();
    void test_the_sources_a_test_watches();
    void test_suggest_onvif();
};
