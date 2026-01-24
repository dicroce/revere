
#include "framework.h"

class test_r_storage : public test_fixture
{
public:
    RTF_FIXTURE(test_r_storage);
#if 0
      TEST(test_r_storage::test_r_dumbdex_writing);
      TEST(test_r_storage::test_r_dumbdex_consistency);
      TEST(test_r_storage::test_r_dumbdex_size_methods);
      TEST(test_r_storage::test_r_dumbdex_full);
      TEST(test_r_storage::test_r_dumbdex_query_first_element);
      TEST(test_r_storage::test_r_ind_block_private_getters);
      TEST(test_r_storage::test_r_ind_block_basic_iteration);
      TEST(test_r_storage::test_r_ind_block_find_lower_bound);
      TEST(test_r_storage::test_r_rel_block_append);
      TEST(test_r_storage::test_r_rel_block_basic_iteration);
      TEST(test_r_storage::test_r_storage_file_basic);
      TEST(test_r_storage::test_r_storage_file_remove_single_block_from_middle);
      TEST(test_r_storage::test_r_storage_file_remove_multiple_block_from_middle);
      TEST(test_r_storage::test_r_storage_file_remove_single_block_from_front);
      TEST(test_r_storage::test_r_storage_file_remove_multiple_block_from_front);
      TEST(test_r_storage::test_r_storage_file_fail_remove_single_block_from_end);
      TEST(test_r_storage::test_r_storage_file_remove_whole_segment);
      TEST(test_r_storage::test_r_storage_file_remove_whole_segment_realistic);
      TEST(test_r_storage::test_r_storage_file_remove_random);
      TEST(test_r_storage::test_r_storage_file_remove_random_then_write);
      TEST(test_r_storage::test_r_storage_file_fake_camera);
      TEST(test_r_storage::test_r_storage_file_file_size_calculation);
      TEST(test_r_storage::test_r_storage_file_human_readable_file_size);
      TEST(test_r_storage::test_r_ring_basic);
#endif
    RTF_FIXTURE_END();

    virtual ~test_r_storage() throw() {}

    virtual void setup();
    virtual void teardown();

#if 0
    void test_r_dumbdex_writing();
    void test_r_dumbdex_consistency();
    void test_r_dumbdex_size_methods();
    void test_r_dumbdex_full();
    void test_r_dumbdex_query_first_element();
    void test_r_ind_block_private_getters();
    void test_r_ind_block_basic_iteration();
    void test_r_ind_block_find_lower_bound();
    void test_r_rel_block_append();
    void test_r_storage_file_basic();
    void test_r_storage_file_remove_single_block_from_middle();
    void test_r_storage_file_remove_multiple_block_from_middle();
    void test_r_storage_file_remove_single_block_from_front();
    void test_r_storage_file_remove_multiple_block_from_front();
    void test_r_storage_file_fail_remove_single_block_from_end();
    void test_r_storage_file_remove_whole_segment();
    void test_r_storage_file_remove_whole_segment_realistic();
    void test_r_storage_file_remove_random();
    void test_r_storage_file_remove_random_then_write();
    void test_r_rel_block_basic_iteration();
    void test_r_storage_file_fake_camera();
    void test_r_storage_file_file_size_calculation();
    void test_r_storage_file_human_readable_file_size();
    void test_r_ring_basic();
    void test_r_nanots_auto_reclaim();
#endif
};
