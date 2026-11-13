#include "framework.h"

class test_r_call_gate : public test_fixture
{
public:
    RTF_FIXTURE(test_r_call_gate);
      TEST(test_r_call_gate::test_closing_with_nothing_inside_returns_at_once);
      TEST(test_r_call_gate::test_a_closed_gate_lets_no_one_in);
      TEST(test_r_call_gate::test_closing_waits_for_a_call_inside);
      TEST(test_r_call_gate::test_closing_gives_up_on_a_call_that_does_not_return);
      TEST(test_r_call_gate::test_opening_lets_calls_in_again);
    RTF_FIXTURE_END();

    virtual ~test_r_call_gate() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_closing_with_nothing_inside_returns_at_once();
    void test_a_closed_gate_lets_no_one_in();
    void test_closing_waits_for_a_call_inside();
    void test_closing_gives_up_on_a_call_that_does_not_return();
    void test_opening_lets_calls_in_again();
};
