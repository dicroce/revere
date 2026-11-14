#include "framework.h"

class test_r_plugin_manifest : public test_fixture
{
public:
    RTF_FIXTURE(test_r_plugin_manifest);
      TEST(test_r_plugin_manifest::test_ecdsa_verify);
      TEST(test_r_plugin_manifest::test_parse);
      TEST(test_r_plugin_manifest::test_parse_rejects);
      TEST(test_r_plugin_manifest::test_check_ok);
      TEST(test_r_plugin_manifest::test_check_signature);
      TEST(test_r_plugin_manifest::test_check_contents);
      TEST(test_r_plugin_manifest::test_names_and_paths);
      TEST(test_r_plugin_manifest::test_plugin_api_versions);
      TEST(test_r_plugin_manifest::test_check_plugin_api);
      TEST(test_r_plugin_manifest::test_load_error_text);
    RTF_FIXTURE_END();

    virtual ~test_r_plugin_manifest() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_ecdsa_verify();
    void test_parse();
    void test_parse_rejects();
    void test_check_ok();
    void test_check_signature();
    void test_check_contents();
    void test_names_and_paths();
    void test_plugin_api_versions();
    void test_check_plugin_api();
    void test_load_error_text();
};
