#include "framework.h"

class test_r_srt_url : public test_fixture
{
public:
    RTF_FIXTURE(test_r_srt_url);
      TEST(test_r_srt_url::test_cloudflares_url);
      TEST(test_r_srt_url::test_the_options_a_caller_needs);
      TEST(test_r_srt_url::test_percent_encoding);
      TEST(test_r_srt_url::test_what_is_refused);
    RTF_FIXTURE_END();

    virtual ~test_r_srt_url() throw() {}
    virtual void setup() {}
    virtual void teardown() {}

    void test_cloudflares_url();
    void test_the_options_a_caller_needs();
    void test_percent_encoding();
    void test_what_is_refused();
};
