#include "framework.h"

class test_r_teardown_queue : public test_fixture
{
public:
    RTF_FIXTURE(test_r_teardown_queue);
      TEST(test_r_teardown_queue::test_destroys_in_order);
      TEST(test_r_teardown_queue::test_pending_until_destroyed);
      TEST(test_r_teardown_queue::test_stuck_teardown_doesnt_block_the_caller);
      TEST(test_r_teardown_queue::test_finish_is_bounded);
      TEST(test_r_teardown_queue::test_push_after_finish);
    RTF_FIXTURE_END();

    virtual ~test_r_teardown_queue() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_destroys_in_order();
    void test_pending_until_destroyed();
    void test_stuck_teardown_doesnt_block_the_caller();
    void test_finish_is_bounded();
    void test_push_after_finish();
};
