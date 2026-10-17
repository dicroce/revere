
#include "framework.h"

class test_r_restream_track : public test_fixture
{
public:
    RTF_FIXTURE(test_r_restream_track);
      TEST(test_r_restream_track::test_gate);
      TEST(test_r_restream_track::test_format_overflow);
      TEST(test_r_restream_track::test_feed_survives_a_pause);
    RTF_FIXTURE_END();

    virtual ~test_r_restream_track() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_gate();
    void test_format_overflow();
    void test_feed_survives_a_pause();
};
