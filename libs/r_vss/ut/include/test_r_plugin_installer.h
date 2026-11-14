#include "framework.h"

class test_r_plugin_installer : public test_fixture
{
public:
    RTF_FIXTURE(test_r_plugin_installer);
      TEST(test_r_plugin_installer::test_parse_url);
      TEST(test_r_plugin_installer::test_resolve_redirect);
      TEST(test_r_plugin_installer::test_release_assets);
      TEST(test_r_plugin_installer::test_install);
      TEST(test_r_plugin_installer::test_install_replaces_an_old_copy);
      TEST(test_r_plugin_installer::test_install_rejects);
      TEST(test_r_plugin_installer::test_release_base_version);
      TEST(test_r_plugin_installer::test_compare_release_versions);
      TEST(test_r_plugin_installer::test_parse_published_releases);
      TEST(test_r_plugin_installer::test_choose_plugin_release);
      TEST(test_r_plugin_installer::test_install_release_build_unchanged);
      TEST(test_r_plugin_installer::test_install_dev_build);
      TEST(test_r_plugin_installer::test_install_dev_build_rejects);
      TEST(test_r_plugin_installer::test_http_get_live);
    RTF_FIXTURE_END();

    virtual ~test_r_plugin_installer() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_parse_url();
    void test_resolve_redirect();
    void test_release_assets();
    void test_install();
    void test_install_replaces_an_old_copy();
    void test_install_rejects();
    void test_release_base_version();
    void test_compare_release_versions();
    void test_parse_published_releases();
    void test_choose_plugin_release();
    void test_install_release_build_unchanged();
    void test_install_dev_build();
    void test_install_dev_build_rejects();
    void test_http_get_live();
};
