#include "framework.h"

class test_r_plugin_loader : public test_fixture
{
public:
    RTF_FIXTURE(test_r_plugin_loader);
      TEST(test_r_plugin_loader::test_choose_copy);
      TEST(test_r_plugin_loader::test_needs_update);
      TEST(test_r_plugin_loader::test_unsigned_build_loads_the_first_copy);
      TEST(test_r_plugin_loader::test_required_skips_unsigned_plugins);
      TEST(test_r_plugin_loader::test_load_and_start);
      TEST(test_r_plugin_loader::test_load_and_start_checks);
      TEST(test_r_plugin_loader::test_uninstall);
      TEST(test_r_plugin_loader::test_uninstall_skipped);
      TEST(test_r_plugin_loader::test_uninstall_when_delete_fails);
      TEST(test_r_plugin_loader::test_plugin_interface);
      TEST(test_r_plugin_loader::test_manifest_interface_checked_before_opening);
      TEST(test_r_plugin_loader::test_unopenable_plugin);
    RTF_FIXTURE_END();

    virtual ~test_r_plugin_loader() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_choose_copy();
    void test_needs_update();
    void test_unsigned_build_loads_the_first_copy();
    void test_required_skips_unsigned_plugins();
    void test_load_and_start();
    void test_load_and_start_checks();
    void test_uninstall();
    void test_uninstall_skipped();
    void test_uninstall_when_delete_fails();
    void test_plugin_interface();
    void test_manifest_interface_checked_before_opening();
    void test_unopenable_plugin();

private:
    std::string _top;
};
