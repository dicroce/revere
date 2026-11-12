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
    void test_http_get_live();
};
