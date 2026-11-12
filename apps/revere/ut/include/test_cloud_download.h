#include "framework.h"

class test_cloud_download : public test_fixture
{
public:
    RTF_FIXTURE(test_cloud_download);
      TEST(test_cloud_download::test_each_platform_gets_its_installer);
      TEST(test_cloud_download::test_other_versions_open_the_latest_release);
      TEST(test_cloud_download::test_install_texts);
      TEST(test_cloud_download::test_this_build_platform);
    RTF_FIXTURE_END();

    virtual ~test_cloud_download() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_each_platform_gets_its_installer();
    void test_other_versions_open_the_latest_release();
    void test_install_texts();
    void test_this_build_platform();
};
