#include "framework.h"

class test_r_doorbell_rings : public test_fixture
{
public:
    RTF_FIXTURE(test_r_doorbell_rings);
      TEST(test_r_doorbell_rings::test_debounce);
      TEST(test_r_doorbell_rings::test_only_a_doorbells_press_rings);
      TEST(test_r_doorbell_rings::test_ring_json);
    RTF_FIXTURE_END();

    virtual ~test_r_doorbell_rings() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_debounce();
    void test_only_a_doorbells_press_rings();
    void test_ring_json();
};
