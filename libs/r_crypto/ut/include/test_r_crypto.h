
#include "framework.h"

class test_r_crypto : public test_fixture
{
public:
    RTF_FIXTURE(test_r_crypto);
      TEST(test_r_crypto::test_ed25519_rfc8032_vectors);
      TEST(test_r_crypto::test_ed25519_wycheproof_vectors);
      TEST(test_r_crypto::test_ed25519_from_seed_leaves_the_seed);
      TEST(test_r_crypto::test_ed25519_sizes);
      TEST(test_r_crypto::test_ed25519_move_wipes_the_source);
      TEST(test_r_crypto::test_ed25519_rejects_non_canonical_points);
    RTF_FIXTURE_END();

    virtual ~test_r_crypto() throw() {}

    virtual void setup();
    virtual void teardown();

    void test_ed25519_rfc8032_vectors();
    void test_ed25519_wycheproof_vectors();
    void test_ed25519_from_seed_leaves_the_seed();
    void test_ed25519_sizes();
    void test_ed25519_move_wipes_the_source();
    void test_ed25519_rejects_non_canonical_points();
};
