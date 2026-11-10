#include "framework.h"

class test_tray_fetch : public test_fixture
{
public:
    RTF_FIXTURE(test_tray_fetch);
      TEST(test_tray_fetch::test_sends_the_local_token);
      TEST(test_tray_fetch::test_reads_the_token_each_time);
      TEST(test_tray_fetch::test_no_token_file_is_refused_not_down);
      TEST(test_tray_fetch::test_nothing_listening);
    RTF_FIXTURE_END();

    virtual ~test_tray_fetch() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_sends_the_local_token();
    void test_reads_the_token_each_time();
    void test_no_token_file_is_refused_not_down();
    void test_nothing_listening();
};
