#include "framework.h"

class test_r_recording_context : public test_fixture
{
public:
    RTF_FIXTURE(test_r_recording_context);
      TEST(test_r_recording_context::test_source_name_prefix);
      TEST(test_r_recording_context::test_a_feed_starts_from_the_stamped_gop_cache);
    RTF_FIXTURE_END();

    virtual ~test_r_recording_context() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_source_name_prefix();
    void test_a_feed_starts_from_the_stamped_gop_cache();
};
