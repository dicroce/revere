#include "framework.h"

class test_r_motion_engine : public test_fixture
{
public:
    RTF_FIXTURE(test_r_motion_engine);
      TEST(test_r_motion_engine::test_event_sink_interface);
      TEST(test_r_motion_engine::test_motion_component_filtering);
      TEST(test_r_motion_engine::test_null_storage_sink_interface);
      TEST(test_r_motion_engine::test_engine_lifecycle);
      TEST(test_r_motion_engine::test_factory_receives_work_item);
      TEST(test_r_motion_engine::test_real_frames_trigger_motion);
      TEST(test_r_motion_engine::test_probe_starts_event_on_first_frame);
      TEST(test_r_motion_engine::test_skips_to_newest_key_frame);
    RTF_FIXTURE_END();

    virtual ~test_r_motion_engine() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_event_sink_interface();
    void test_motion_component_filtering();
    void test_null_storage_sink_interface();
    void test_engine_lifecycle();
    void test_factory_receives_work_item();
    void test_real_frames_trigger_motion();
    void test_probe_starts_event_on_first_frame();
    void test_skips_to_newest_key_frame();
};
