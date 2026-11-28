#include "framework.h"

class test_r_doorbell_rings : public test_fixture
{
public:
    RTF_FIXTURE(test_r_doorbell_rings);
      TEST(test_r_doorbell_rings::test_debounce);
      TEST(test_r_doorbell_rings::test_one_ring_across_sources);
      TEST(test_r_doorbell_rings::test_last_press_by_source);
      TEST(test_r_doorbell_rings::test_a_source_the_doorbell_isnt_set_to);
      TEST(test_r_doorbell_rings::test_only_a_doorbells_press_rings);
      TEST(test_r_doorbell_rings::test_ring_json);
    RTF_FIXTURE_END();

    virtual ~test_r_doorbell_rings() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_debounce();
    void test_one_ring_across_sources();
    void test_last_press_by_source();
    void test_a_source_the_doorbell_isnt_set_to();
    void test_only_a_doorbells_press_rings();
    void test_ring_json();
};
