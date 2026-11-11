#include "framework.h"

class test_r_substream_setting : public test_fixture
{
public:
    RTF_FIXTURE(test_r_substream_setting);
      TEST(test_r_substream_setting::test_json);
      TEST(test_r_substream_setting::test_off_until_turned_on_and_it_persists);
    RTF_FIXTURE_END();

    virtual ~test_r_substream_setting() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_json();
    void test_off_until_turned_on_and_it_persists();
};
