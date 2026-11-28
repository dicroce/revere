#include "framework.h"

class test_r_camera_kind : public test_fixture
{
public:
    RTF_FIXTURE(test_r_camera_kind);
      TEST(test_r_camera_kind::test_update_properties_sets_kind);
    RTF_FIXTURE_END();

    virtual ~test_r_camera_kind() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_update_properties_sets_kind();
};
