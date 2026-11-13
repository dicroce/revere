#include "framework.h"

class test_r_plugin_notifier : public test_fixture
{
public:
    RTF_FIXTURE(test_r_plugin_notifier);
      TEST(test_r_plugin_notifier::test_runs_calls_in_order);
      TEST(test_r_plugin_notifier::test_nothing_until_accepting);
      TEST(test_r_plugin_notifier::test_a_slow_call_does_not_hold_up_post);
      TEST(test_r_plugin_notifier::test_overflow_drops_the_oldest);
      TEST(test_r_plugin_notifier::test_a_throwing_call_does_not_stop_the_rest);
      TEST(test_r_plugin_notifier::test_stopping_accepting_drops_what_is_queued);
      TEST(test_r_plugin_notifier::test_stopping_accepting_waits_for_the_running_call);
      TEST(test_r_plugin_notifier::test_stopping_accepting_from_inside_a_call_does_not_wait_for_itself);
      TEST(test_r_plugin_notifier::test_destructor_waits_only_for_the_running_call);
    RTF_FIXTURE_END();

    virtual ~test_r_plugin_notifier() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_runs_calls_in_order();
    void test_nothing_until_accepting();
    void test_a_slow_call_does_not_hold_up_post();
    void test_overflow_drops_the_oldest();
    void test_a_throwing_call_does_not_stop_the_rest();
    void test_stopping_accepting_drops_what_is_queued();
    void test_stopping_accepting_waits_for_the_running_call();
    void test_stopping_accepting_from_inside_a_call_does_not_wait_for_itself();
    void test_destructor_waits_only_for_the_running_call();
};
