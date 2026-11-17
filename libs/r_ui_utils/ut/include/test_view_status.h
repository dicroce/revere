#include "framework.h"

class test_view_status : public test_fixture
{
public:
    RTF_FIXTURE(test_view_status);
      TEST(test_view_status::test_start_steps);
      TEST(test_view_status::test_playing_says_nothing);
      TEST(test_view_status::test_routine_gap_unlabelled);
      TEST(test_view_status::test_long_freeze);
      TEST(test_view_status::test_restart);
      TEST(test_view_status::test_not_live);
      TEST(test_view_status::test_label_holds);
      TEST(test_view_status::test_texts);
      TEST(test_view_status::test_check_pane);
      TEST(test_view_status::test_quiet_playback_pauses);
    RTF_FIXTURE_END();

    virtual ~test_view_status() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_start_steps();
    void test_playing_says_nothing();
    void test_routine_gap_unlabelled();
    void test_long_freeze();
    void test_restart();
    void test_not_live();
    void test_label_holds();
    void test_texts();
    void test_check_pane();
    void test_quiet_playback_pauses();
};
