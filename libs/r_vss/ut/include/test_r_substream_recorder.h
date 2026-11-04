#include "framework.h"

class test_r_substream_recorder : public test_fixture
{
public:
    RTF_FIXTURE(test_r_substream_recorder);
      TEST(test_r_substream_recorder::test_ring_path);
      TEST(test_r_substream_recorder::test_next_step);
      TEST(test_r_substream_recorder::test_records_into_its_ring);
      TEST(test_r_substream_recorder::test_no_camera);
    RTF_FIXTURE_END();

    virtual ~test_r_substream_recorder() throw() {}
    virtual void setup();
    virtual void teardown();

    void test_ring_path();
    void test_next_step();
    void test_records_into_its_ring();
    void test_no_camera();
};
