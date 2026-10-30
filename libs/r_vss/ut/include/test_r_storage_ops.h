#include "framework.h"

class test_r_storage_ops : public test_fixture
{
public:
    RTF_FIXTURE(test_r_storage_ops);
      TEST(test_r_storage_ops::test_rename_moves_then_commits);
      TEST(test_r_storage_ops::test_copy_keeps_sources_until_commit);
      TEST(test_r_storage_ops::test_cancel_partway_puts_everything_back);
      TEST(test_r_storage_ops::test_cancel_between_files);
      TEST(test_r_storage_ops::test_failed_save_puts_everything_back);
      TEST(test_r_storage_ops::test_failed_copy_leaves_no_partial_file);
    RTF_FIXTURE_END();

    virtual ~test_r_storage_ops() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_rename_moves_then_commits();
    void test_copy_keeps_sources_until_commit();
    void test_cancel_partway_puts_everything_back();
    void test_cancel_between_files();
    void test_failed_save_puts_everything_back();
    void test_failed_copy_leaves_no_partial_file();
};
