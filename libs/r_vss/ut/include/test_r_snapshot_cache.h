
#include "framework.h"

class test_r_snapshot_cache : public test_fixture
{
public:
    RTF_FIXTURE(test_r_snapshot_cache);
      TEST(test_r_snapshot_cache::test_concurrent_requests_share_one_build);
      TEST(test_r_snapshot_cache::test_reused_within_ttl_then_rebuilt);
      TEST(test_r_snapshot_cache::test_different_keys_build_separately);
      TEST(test_r_snapshot_cache::test_failure_is_not_kept);
      TEST(test_r_snapshot_cache::test_entries_are_bounded);
    RTF_FIXTURE_END();

    virtual ~test_r_snapshot_cache() throw() {}

    virtual void setup() {}
    virtual void teardown() {}

    void test_concurrent_requests_share_one_build();
    void test_reused_within_ttl_then_rebuilt();
    void test_different_keys_build_separately();
    void test_failure_is_not_kept();
    void test_entries_are_bounded();
};
