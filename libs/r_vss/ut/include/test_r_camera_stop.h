#include "framework.h"
#include <string>

class test_r_camera_stop : public test_fixture
{
public:
    RTF_FIXTURE(test_r_camera_stop);
      TEST(test_r_camera_stop::test_stop_and_start_an_assigned_camera);
      TEST(test_r_camera_stop::test_only_assigned_cameras_stop);
    RTF_FIXTURE_END();

    virtual ~test_r_camera_stop() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_stop_and_start_an_assigned_camera();
    void test_only_assigned_cameras_stop();

private:
    std::string _top;
};
