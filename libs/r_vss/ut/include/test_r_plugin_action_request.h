#include "framework.h"

class test_r_plugin_action_request : public test_fixture
{
public:
    RTF_FIXTURE(test_r_plugin_action_request);
      TEST(test_r_plugin_action_request::test_offered_actions);
      TEST(test_r_plugin_action_request::test_needs_a_token);
      TEST(test_r_plugin_action_request::test_runs_an_offered_action);
      TEST(test_r_plugin_action_request::test_runs_a_choice_pick);
      TEST(test_r_plugin_action_request::test_refuses_what_is_not_offered);
      TEST(test_r_plugin_action_request::test_refuses_bad_requests);
    RTF_FIXTURE_END();

    virtual ~test_r_plugin_action_request() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_offered_actions();
    void test_needs_a_token();
    void test_runs_an_offered_action();
    void test_runs_a_choice_pick();
    void test_refuses_what_is_not_offered();
    void test_refuses_bad_requests();
};
