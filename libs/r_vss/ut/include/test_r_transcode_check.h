#include "framework.h"

class test_r_transcode_check : public test_fixture
{
public:
    RTF_FIXTURE(test_r_transcode_check);
      TEST(test_r_transcode_check::test_mostly_failed);
    RTF_FIXTURE_END();

    virtual ~test_r_transcode_check() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_mostly_failed();
};
