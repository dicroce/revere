#include "framework.h"

class test_r_restream_mount : public test_fixture
{
public:
    RTF_FIXTURE(test_r_restream_mount);
      TEST(test_r_restream_mount::test_url);
      TEST(test_r_restream_mount::test_url_reaches_the_mount);
    RTF_FIXTURE_END();

    virtual ~test_r_restream_mount() throw() {}

    virtual void setup();
    virtual void teardown() {}

    void test_url();
    void test_url_reaches_the_mount();
};
