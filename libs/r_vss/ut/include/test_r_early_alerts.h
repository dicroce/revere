#include "framework.h"

class test_r_early_alerts : public test_fixture
{
public:
    RTF_FIXTURE(test_r_early_alerts);
      TEST(test_r_early_alerts::test_box_iou);
      TEST(test_r_early_alerts::test_floor_and_once_per_class);
      TEST(test_r_early_alerts::test_look_interval);
      TEST(test_r_early_alerts::test_parked_car_anchor);
      TEST(test_r_early_alerts::test_jittery_leaf);
      TEST(test_r_early_alerts::test_doorstep_visitors);
      TEST(test_r_early_alerts::test_passer_by_is_no_anchor);
      TEST(test_r_early_alerts::test_anchor_lapses);
      TEST(test_r_early_alerts::test_anchor_cap);
      TEST(test_r_early_alerts::test_json_round_trip);
      TEST(test_r_early_alerts::test_probe_confirms);
    RTF_FIXTURE_END();

    virtual ~test_r_early_alerts() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_box_iou();
    void test_floor_and_once_per_class();
    void test_look_interval();
    void test_parked_car_anchor();
    void test_jittery_leaf();
    void test_doorstep_visitors();
    void test_passer_by_is_no_anchor();
    void test_anchor_lapses();
    void test_anchor_cap();
    void test_json_round_trip();
    void test_probe_confirms();
};
