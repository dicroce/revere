#include "framework.h"

class test_r_cloud_install : public test_fixture
{
public:
    RTF_FIXTURE(test_r_cloud_install);
      TEST(test_r_cloud_install::test_release_version);
      TEST(test_r_cloud_install::test_states);
      TEST(test_r_cloud_install::test_start_refused);
      TEST(test_r_cloud_install::test_install_and_load);
      TEST(test_r_cloud_install::test_failed_install);
      TEST(test_r_cloud_install::test_terms_kept);
    RTF_FIXTURE_END();

    virtual ~test_r_cloud_install() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_release_version();
    void test_states();
    void test_start_refused();
    void test_install_and_load();
    void test_failed_install();
    void test_terms_kept();
};
