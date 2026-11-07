
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
      TEST(test_r_crypto::test_random_bytes);
      TEST(test_r_crypto::test_x25519_rfc7748);
      TEST(test_r_crypto::test_x25519_wycheproof_vectors);
      TEST(test_r_crypto::test_x25519_generate);
      TEST(test_r_crypto::test_x25519_sizes);
      TEST(test_r_crypto::test_x25519_move_wipes_the_source);
      TEST(test_r_crypto::test_hkdf_sha512_wycheproof_vectors);
      TEST(test_r_crypto::test_chacha20_poly1305_wycheproof_vectors);
      TEST(test_r_crypto::test_chacha20_poly1305_tampering);
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
    void test_random_bytes();
    void test_x25519_rfc7748();
    void test_x25519_wycheproof_vectors();
    void test_x25519_generate();
    void test_x25519_sizes();
    void test_x25519_move_wipes_the_source();
    void test_hkdf_sha512_wycheproof_vectors();
    void test_chacha20_poly1305_wycheproof_vectors();
    void test_chacha20_poly1305_tampering();
};
