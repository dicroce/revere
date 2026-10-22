#include "framework.h"

class test_r_recording_context : public test_fixture
{
public:
    RTF_FIXTURE(test_r_recording_context);
      TEST(test_r_recording_context::test_source_name_prefix);
    RTF_FIXTURE_END();

    virtual ~test_r_recording_context() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_source_name_prefix();
};
