
#include "test_r_recording_context.h"
#include "r_vss/r_recording_context.h"
#include "r_disco/r_camera.h"

using namespace std;
using namespace r_vss;
using namespace r_disco;

REGISTER_TEST_FIXTURE(test_r_recording_context);

void test_r_recording_context::test_source_name_prefix()
{
    r_camera camera;
    camera.id = "cam-1234";

    // No friendly name: the camera id
    RTF_ASSERT(recording_source_name_prefix(camera) == "cam-1234_");

    camera.friendly_name.set_value(string());
    RTF_ASSERT(recording_source_name_prefix(camera) == "cam-1234_");

    // A friendly name: used as is
    camera.friendly_name.set_value("Raccoon Cam 5");
    RTF_ASSERT(recording_source_name_prefix(camera) == "Raccoon Cam 5_");
}
