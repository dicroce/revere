#include "framework.h"

class test_render_policy : public test_fixture
{
public:
    RTF_FIXTURE(test_render_policy);
      TEST(test_render_policy::test_renderer_mode_from_env);
      TEST(test_render_policy::test_attempts_per_platform);
      TEST(test_render_policy::test_forced_modes);
      TEST(test_render_policy::test_window_drawable);
      TEST(test_render_policy::test_hash_tells_frames_apart);
      TEST(test_render_policy::test_frame_skipper);
      TEST(test_render_policy::test_frame_skipper_invalidate);
    RTF_FIXTURE_END();

    virtual ~test_render_policy() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_renderer_mode_from_env();
    void test_attempts_per_platform();
    void test_forced_modes();
    void test_window_drawable();
    void test_hash_tells_frames_apart();
    void test_frame_skipper();
    void test_frame_skipper_invalidate();
};
