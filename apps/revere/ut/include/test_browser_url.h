#include "framework.h"

class test_browser_url : public test_fixture
{
public:
    RTF_FIXTURE(test_browser_url);
      TEST(test_browser_url::test_web_urls_allowed);
      TEST(test_browser_url::test_other_schemes_refused);
      TEST(test_browser_url::test_malformed_refused);
    RTF_FIXTURE_END();

    virtual ~test_browser_url() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_web_urls_allowed();
    void test_other_schemes_refused();
    void test_malformed_refused();
};
