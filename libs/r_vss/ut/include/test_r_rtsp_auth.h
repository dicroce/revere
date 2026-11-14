
#include "framework.h"

class test_r_rtsp_auth : public test_fixture
{
public:
    RTF_FIXTURE(test_r_rtsp_auth);
      TEST(test_r_rtsp_auth::test_password_persisted);
      TEST(test_r_rtsp_auth::test_digest_required);
      TEST(test_r_rtsp_auth::test_by_id_mount);
    RTF_FIXTURE_END();

    virtual ~test_r_rtsp_auth() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_password_persisted();
    void test_digest_required();
    void test_by_id_mount();
};
