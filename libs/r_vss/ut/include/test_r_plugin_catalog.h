#include "framework.h"

class test_r_plugin_catalog : public test_fixture
{
public:
    RTF_FIXTURE(test_r_plugin_catalog);
      TEST(test_r_plugin_catalog::test_parse);
      TEST(test_r_plugin_catalog::test_parse_generated);
      TEST(test_r_plugin_catalog::test_parse_rejects);
      TEST(test_r_plugin_catalog::test_check_signature);
      TEST(test_r_plugin_catalog::test_check_rejects);
      TEST(test_r_plugin_catalog::test_assets);
      TEST(test_r_plugin_catalog::test_choose_release);
      TEST(test_r_plugin_catalog::test_fetch_release_build);
      TEST(test_r_plugin_catalog::test_fetch_dev_build);
      TEST(test_r_plugin_catalog::test_fetch_missing_for_platform);
      TEST(test_r_plugin_catalog::test_state);
      TEST(test_r_plugin_catalog::test_install_second_plugin);
      TEST(test_r_plugin_catalog::test_install_link_throws);
      TEST(test_r_plugin_catalog::test_terms);
    RTF_FIXTURE_END();

    virtual ~test_r_plugin_catalog() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_parse();
    void test_parse_generated();
    void test_parse_rejects();
    void test_check_signature();
    void test_check_rejects();
    void test_assets();
    void test_choose_release();
    void test_fetch_release_build();
    void test_fetch_dev_build();
    void test_fetch_missing_for_platform();
    void test_state();
    void test_install_second_plugin();
    void test_install_link_throws();
    void test_terms();
};
