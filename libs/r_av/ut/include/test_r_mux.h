
#include "framework.h"

class test_r_mux : public test_fixture
{
public:
    RTF_FIXTURE(test_r_mux);
      TEST(test_r_mux::test_basic_demux);
      TEST(test_r_mux::test_basic_mux);
    RTF_FIXTURE_END();

    virtual ~test_r_mux() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_basic_demux();
    void test_basic_mux();
};
