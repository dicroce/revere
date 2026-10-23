#include "framework.h"

class test_r_key_frame_reply : public test_fixture
{
public:
    RTF_FIXTURE(test_r_key_frame_reply);
      TEST(test_r_key_frame_reply::test_newest_recorded_key_frame);
      TEST(test_r_key_frame_reply::test_nv12);
      TEST(test_r_key_frame_reply::test_nothing_recorded);
      TEST(test_r_key_frame_reply::test_not_a_reply);
    RTF_FIXTURE_END();

    virtual ~test_r_key_frame_reply() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_newest_recorded_key_frame();
    void test_nv12();
    void test_nothing_recorded();
    void test_not_a_reply();
};
