#include "test_r_crypto.h"
#include "ed25519_vectors.h"
#include "srp6a_vectors.h"
#include "r_crypto/r_ed25519.h"
#include "r_crypto/r_srp6a.h"
#include "x25519_hkdf_aead_vectors.h"
#include "r_crypto/r_random.h"
#include "r_crypto/r_x25519.h"
#include "r_crypto/r_hkdf.h"
#include "r_crypto/r_chacha20_poly1305.h"
#include "r_utils/r_exception.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <memory>
#include <vector>

using namespace std;
using namespace r_utils;
using namespace r_crypto;

REGISTER_TEST_FIXTURE(test_r_crypto);

void setup_global()
{
}

void teardown_global()
{
}

void test_r_crypto::setup()
{
}

void test_r_crypto::teardown()
{
}

static vector<uint8_t> unhex(const char* hex)
{
    vector<uint8_t> output;
    for(size_t i = 0; hex[i] && hex[i+1]; i += 2)
        output.push_back((uint8_t)stoul(string(hex + i, 2), nullptr, 16));
    return output;
}

template<size_t N>
static vector<uint8_t> bytes(const array<uint8_t, N>& a)
{
    return vector<uint8_t>(a.begin(), a.end());
}

void test_r_crypto::test_ed25519_rfc8032_vectors()
{
    // The seed's public key and the deterministic signature are RFC 8032's,
    // which a BLAKE2b EdDSA (Monocypher's default) would not give.
    for(auto& v : RFC8032_ED25519)
    {
        auto seed = unhex(v.seed);
        auto msg = unhex(v.msg);
        auto pair = r_ed25519_key_pair::from_seed(seed.data(), seed.size());
        RTF_ASSERT(bytes(pair.public_key()) == unhex(v.public_key));

        auto signature = pair.sign(msg.data(), msg.size());
        RTF_ASSERT(bytes(signature) == unhex(v.signature));

        auto pk = pair.public_key();
        RTF_ASSERT(ed25519_verify(pk.data(), pk.size(), msg.data(), msg.size(), signature.data(), signature.size()));

        // A message one bit different, or a signature one bit different, doesn't verify.
        auto other = msg;
        if(other.empty())
            other.push_back(0);
        else other[0] ^= 1;
        RTF_ASSERT(!ed25519_verify(pk.data(), pk.size(), other.data(), other.size(), signature.data(), signature.size()));
        signature[63] ^= 0x10;
        RTF_ASSERT(!ed25519_verify(pk.data(), pk.size(), msg.data(), msg.size(), signature.data(), signature.size()));
    }
}

void test_r_crypto::test_ed25519_wycheproof_vectors()
{
    // Every valid vector verifies and every invalid one doesn't: edge-case S and R
    // values, a non-canonical S (malleability), truncated or padded signatures.
    // Acceptable ones (small- or mixed-order public keys) may go either way.
    // Every disagreement is printed before the assert, so an upgrade shows them all.
    size_t valid = 0, invalid = 0, wrong = 0;
    for(auto& v : WYCHEPROOF_ED25519)
    {
        auto pk = unhex(v.public_key);
        auto msg = unhex(v.msg);
        auto sig = unhex(v.signature);
        bool ok = ed25519_verify(pk.data(), pk.size(), msg.data(), msg.size(), sig.data(), sig.size());
        if(v.result == R_VALID)
        {
            ++valid;
            if(!ok)
            {
                printf("wycheproof tcId %d (%s) should verify\n", v.tc_id, v.flag);
                ++wrong;
            }
        }
        else if(v.result == R_INVALID)
        {
            ++invalid;
            if(ok)
            {
                printf("wycheproof tcId %d (%s) should not verify\n", v.tc_id, v.flag);
                ++wrong;
            }
        }
    }
    RTF_ASSERT_EQUAL(valid, (size_t)117);
    RTF_ASSERT_EQUAL(invalid, (size_t)64);
    RTF_ASSERT_EQUAL(wrong, (size_t)0);
}

void test_r_crypto::test_ed25519_from_seed_leaves_the_seed()
{
    // Monocypher wipes the seed it's given; the caller's must survive, so a stored
    // seed can make the same key again.
    auto seed = unhex(RFC8032_ED25519[0].seed);
    auto before = seed;
    auto first = r_ed25519_key_pair::from_seed(seed.data(), seed.size());
    RTF_ASSERT(seed == before);
    auto second = r_ed25519_key_pair::from_seed(seed.data(), seed.size());
    RTF_ASSERT(first.public_key() == second.public_key());
}

void test_r_crypto::test_ed25519_sizes()
{
    auto seed = unhex(RFC8032_ED25519[0].seed);
    RTF_ASSERT_THROWS(r_ed25519_key_pair::from_seed(seed.data(), 31), r_invalid_argument_exception);
    RTF_ASSERT_THROWS(r_ed25519_key_pair::from_seed(seed.data(), 33), r_invalid_argument_exception);
    RTF_ASSERT_THROWS(r_ed25519_key_pair::from_seed(nullptr, 32), r_invalid_argument_exception);

    auto pair = r_ed25519_key_pair::from_seed(seed.data(), seed.size());
    uint8_t msg[] = {1, 2, 3};
    auto pk = pair.public_key();
    auto sig = pair.sign(msg, sizeof(msg));
    RTF_ASSERT(ed25519_verify(pk.data(), 32, msg, sizeof(msg), sig.data(), 64));

    // The right bytes with a wrong size given never verify.
    RTF_ASSERT(!ed25519_verify(pk.data(), 31, msg, sizeof(msg), sig.data(), 64));
    RTF_ASSERT(!ed25519_verify(pk.data(), 32, msg, sizeof(msg), sig.data(), 63));
    vector<uint8_t> longer(sig.begin(), sig.end());
    longer.push_back(0);
    RTF_ASSERT(!ed25519_verify(pk.data(), 32, msg, sizeof(msg), longer.data(), longer.size()));
    RTF_ASSERT(!ed25519_verify(nullptr, 32, msg, sizeof(msg), sig.data(), 64));
    RTF_ASSERT(!ed25519_verify(pk.data(), 32, msg, sizeof(msg), nullptr, 64));
}

void test_r_crypto::test_ed25519_move_wipes_the_source()
{
    auto seed = unhex(RFC8032_ED25519[0].seed);
    auto pair = r_ed25519_key_pair::from_seed(seed.data(), seed.size());
    auto pk = pair.public_key();
    auto moved = std::move(pair);
    RTF_ASSERT(moved.public_key() == pk);
    // The moved-from pair holds nothing of the key any more.
    array<uint8_t, 32> zero {};
    RTF_ASSERT(pair.public_key() == zero);

    auto other = r_ed25519_key_pair::from_seed(unhex(RFC8032_ED25519[1].seed).data(), 32);
    other = std::move(moved);
    RTF_ASSERT(other.public_key() == pk);
    RTF_ASSERT(moved.public_key() == zero);
}

void test_r_crypto::test_ed25519_rejects_non_canonical_points()
{
    // Each wrong encoding beside its canonical twin, which verifies through a
    // small-order point: so the wrong one fails for its encoding alone. With A the
    // identity, R = B and s = 1 verify for any message; with A = (0, -1) they do
    // when h is even, and with y = 0 when h is a multiple of 4, which each message
    // gives for its key's bytes.
    const char* B = "5866666666666666666666666666666666666666666666666666666666666666";
    const char* ONE = "0100000000000000000000000000000000000000000000000000000000000000";
    const char* ZERO = "0000000000000000000000000000000000000000000000000000000000000000";
    const char* IDENTITY = "0100000000000000000000000000000000000000000000000000000000000000";
    const char* IDENTITY_SIGN = "0100000000000000000000000000000000000000000000000000000000000080";
    const char* IDENTITY_P_PLUS_1 = "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f";
    const char* MINUS_ONE = "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f";
    const char* MINUS_ONE_SIGN = "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    // y = 0 is a point of order 4; y = p is the same y written wrongly.
    const char* Y_ZERO = "0000000000000000000000000000000000000000000000000000000000000000";
    const char* Y_ZERO_AS_P = "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f";

    auto sig = [](const char* r, const char* s) {
        auto v = unhex(r);
        auto t = unhex(s);
        v.insert(v.end(), t.begin(), t.end());
        return v;
    };
    auto verifies = [](const char* a, const string& m, const vector<uint8_t>& s) {
        auto pk = unhex(a);
        return ed25519_verify(pk.data(), pk.size(), (const uint8_t*)m.data(), m.size(), s.data(), s.size());
    };

    // The public key.
    RTF_ASSERT(verifies(IDENTITY, "abc", sig(B, ONE)));
    RTF_ASSERT(!verifies(IDENTITY_SIGN, "abc", sig(B, ONE)));
    RTF_ASSERT(!verifies(IDENTITY_P_PLUS_1, "abc", sig(B, ONE)));
    RTF_ASSERT(verifies(MINUS_ONE, "revere 1", sig(B, ONE)));
    RTF_ASSERT(!verifies(MINUS_ONE_SIGN, "revere 0", sig(B, ONE)));
    RTF_ASSERT(verifies(Y_ZERO, "revere 0", sig(B, ONE)));
    RTF_ASSERT(!verifies(Y_ZERO_AS_P, "revere 0", sig(B, ONE)));

    // R, with A the identity and s = 0.
    RTF_ASSERT(verifies(IDENTITY, "abc", sig(IDENTITY, ZERO)));
    RTF_ASSERT(!verifies(IDENTITY, "abc", sig(IDENTITY_SIGN, ZERO)));
    RTF_ASSERT(!verifies(IDENTITY, "abc", sig(IDENTITY_P_PLUS_1, ZERO)));
}

static r_srp6a_group srp_group(const srp6a_vector& v)
{
    return v.group_bits == 1024 ? r_srp6a_group::rfc5054_1024 : r_srp6a_group::rfc5054_3072;
}

static r_srp6a_hash srp_hash(const srp6a_vector& v)
{
    return string(v.hash) == "sha1" ? r_srp6a_hash::sha1 : r_srp6a_hash::sha512;
}

static unique_ptr<r_srp6a_server> srp_server(const srp6a_vector& v)
{
    return r_srp6a_server::for_known_answer_test(srp_group(v), srp_hash(v), v.username, unhex(v.salt), unhex(v.verifier), unhex(v.b));
}

void test_r_crypto::test_srp6a_known_answers()
{
    // RFC 5054 appendix B (its verifier, B and premaster secret, through K = H(S)),
    // and 3072-bit SHA-512 exchanges whose proofs and key an independent SRP-6a
    // implementation agrees with: one plain, one with an A a byte short, one with
    // an S a byte short.
    for(auto& v : SRP6A_VECTORS)
    {
        RTF_ASSERT(srp6a_verifier(srp_group(v), srp_hash(v), v.username, v.password, unhex(v.salt)) == unhex(v.verifier));

        auto server = srp_server(v);
        RTF_ASSERT(server->public_key() == unhex(v.B));

        auto A = unhex(v.A);
        auto M1 = unhex(v.M1);
        RTF_ASSERT(server->verify_client(A.data(), A.size(), M1.data(), M1.size()));
        RTF_ASSERT(server->server_proof() == unhex(v.M2));
        RTF_ASSERT(server->session_key() == unhex(v.K));
    }
}

void test_r_crypto::test_srp6a_refuses_A_that_makes_S_0()
{
    // A = 0, N or 2N gives S = 0 whatever the password, so a client that knows no
    // password could send the proof for K = H(0). Each is refused (RFC 5054 2.5.4).
    for(auto& v : SRP6A_VECTORS)
    {
        auto B = unhex(v.B);
        size_t size = B.size();

        vector<uint8_t> zero(size, 0);
        auto cheat0 = unhex(v.M1_if_S_is_0_for_A_0);
        RTF_ASSERT(!srp_server(v)->verify_client(zero.data(), zero.size(), cheat0.data(), cheat0.size()));

        auto server = srp_server(v);
        // N itself, as the vectors give it.
        auto cheatN = unhex(v.M1_if_S_is_0_for_A_N);
        auto n = unhex(v.group_bits == 1024 ? SRP6A_N_1024 : SRP6A_N_3072);
        RTF_ASSERT(!server->verify_client(n.data(), n.size(), cheatN.data(), cheatN.size()));
        RTF_ASSERT_THROWS(server->session_key(), r_internal_exception);

        // 2N is one byte longer than N.
        vector<uint8_t> two_n(n.size() + 1, 0);
        int carry = 0;
        for(size_t i = n.size(); i > 0; --i)
        {
            int d = n[i-1] * 2 + carry;
            two_n[i] = (uint8_t)(d & 0xff);
            carry = d >> 8;
        }
        two_n[0] = (uint8_t)carry;
        RTF_ASSERT(!srp_server(v)->verify_client(two_n.data(), two_n.size(), cheatN.data(), cheatN.size()));

        // No A at all.
        RTF_ASSERT(!srp_server(v)->verify_client(zero.data(), 0, cheat0.data(), cheat0.size()));
    }
}

void test_r_crypto::test_srp6a_one_try()
{
    // A wrong proof spends the server: the right one after it fails too, so each
    // guess at the password costs a new exchange.
    auto& v = SRP6A_VECTORS[1];
    auto server = srp_server(v);
    auto A = unhex(v.A);
    auto M1 = unhex(v.M1);
    auto wrong = M1;
    wrong[10] ^= 0x01;
    RTF_ASSERT(!server->verify_client(A.data(), A.size(), wrong.data(), wrong.size()));
    RTF_ASSERT(!server->verify_client(A.data(), A.size(), M1.data(), M1.size()));
    RTF_ASSERT_THROWS(server->server_proof(), r_internal_exception);
    RTF_ASSERT_THROWS(server->session_key(), r_internal_exception);

    // A refused before any work counts as the try too.
    auto refused = srp_server(v);
    vector<uint8_t> zero(A.size(), 0);
    RTF_ASSERT(!refused->verify_client(zero.data(), zero.size(), M1.data(), M1.size()));
    RTF_ASSERT(!refused->verify_client(A.data(), A.size(), M1.data(), M1.size()));

    // And a right one can't be used twice.
    auto again = srp_server(v);
    RTF_ASSERT(again->verify_client(A.data(), A.size(), M1.data(), M1.size()));
    RTF_ASSERT(!again->verify_client(A.data(), A.size(), M1.data(), M1.size()));
}

void test_r_crypto::test_srp6a_wrong_password()
{
    // The vector's client, against a verifier made from another password.
    auto& v = SRP6A_VECTORS[1];
    auto other = srp6a_verifier(srp_group(v), srp_hash(v), v.username, "correct horse battery!", unhex(v.salt));
    RTF_ASSERT(other != unhex(v.verifier));
    auto server = r_srp6a_server::for_known_answer_test(srp_group(v), srp_hash(v), v.username, unhex(v.salt), other, unhex(v.b));
    auto A = unhex(v.A);
    auto M1 = unhex(v.M1);
    RTF_ASSERT(!server->verify_client(A.data(), A.size(), M1.data(), M1.size()));
}

void test_r_crypto::test_srp6a_proof_sizes()
{
    // The right proof with a byte more, or a byte less, isn't the proof.
    auto& v = SRP6A_VECTORS[1];
    auto A = unhex(v.A);
    auto M1 = unhex(v.M1);
    auto longer = M1;
    longer.push_back(0);
    RTF_ASSERT(!srp_server(v)->verify_client(A.data(), A.size(), longer.data(), longer.size()));
    RTF_ASSERT(!srp_server(v)->verify_client(A.data(), A.size(), M1.data(), M1.size() - 1));
    RTF_ASSERT(!srp_server(v)->verify_client(A.data(), A.size(), nullptr, M1.size()));

    RTF_ASSERT_THROWS(r_srp6a_server(srp_group(v), srp_hash(v), v.username, unhex(v.salt), vector<uint8_t>()), r_invalid_argument_exception);
    RTF_ASSERT_THROWS(r_srp6a_server(srp_group(v), srp_hash(v), v.username, unhex(v.salt), vector<uint8_t>(385, 1)), r_invalid_argument_exception);
}

void test_r_crypto::test_srp6a_random_b()
{
    // Each server draws its own b, and its B fills all of N's bytes.
    auto& v = SRP6A_VECTORS[1];
    r_srp6a_server one(srp_group(v), srp_hash(v), v.username, unhex(v.salt), unhex(v.verifier));
    r_srp6a_server two(srp_group(v), srp_hash(v), v.username, unhex(v.salt), unhex(v.verifier));
    RTF_ASSERT(one.public_key() != two.public_key());
    RTF_ASSERT(one.public_key() != unhex(v.B));
    RTF_ASSERT_EQUAL(one.public_key().size(), (size_t)384);
    RTF_ASSERT(one.public_key()[0] != 0);
    RTF_ASSERT(two.public_key()[0] != 0);
}

void test_r_crypto::test_srp6a_refuses_A_plus_N()
{
    // A + N is the same A mod N, and the proof over its bytes is right for the
    // password; A must still be below N. The same client with A itself passes.
    auto& v = SRP6A_VECTORS[0];
    auto A = unhex(SRP6A_RFC_A);
    auto M1 = unhex(SRP6A_RFC_M1_FOR_A);
    RTF_ASSERT(srp_server(v)->verify_client(A.data(), A.size(), M1.data(), M1.size()));

    auto A_plus_N = unhex(SRP6A_RFC_A_PLUS_N);
    auto M1_plus_N = unhex(SRP6A_RFC_M1_FOR_A_PLUS_N);
    RTF_ASSERT_EQUAL(A_plus_N.size(), (size_t)128);
    RTF_ASSERT(!srp_server(v)->verify_client(A_plus_N.data(), A_plus_N.size(), M1_plus_N.data(), M1_plus_N.size()));
}

void test_r_crypto::test_srp6a_known_answer_b_is_checked()
{
    // b = 0 would make S = 1 for any password; a short b, or one not below N, is
    // refused too.
    auto& v = SRP6A_VECTORS[1];
    auto make = [&](const vector<uint8_t>& b) {
        return r_srp6a_server::for_known_answer_test(srp_group(v), srp_hash(v), v.username, unhex(v.salt), unhex(v.verifier), b);
    };
    RTF_ASSERT_THROWS(make(vector<uint8_t>()), r_invalid_argument_exception);
    RTF_ASSERT_THROWS(make(vector<uint8_t>(31, 0x55)), r_invalid_argument_exception);
    RTF_ASSERT_THROWS(make(vector<uint8_t>(32, 0)), r_invalid_argument_exception);
    RTF_ASSERT_THROWS(make(vector<uint8_t>(384, 0xff)), r_invalid_argument_exception);
    RTF_ASSERT_THROWS(make(unhex(SRP6A_N_3072)), r_invalid_argument_exception);
    RTF_ASSERT(make(unhex(v.b))->public_key() == unhex(v.B));
}

static bool all_zero(const vector<uint8_t>& v)
{
    for(auto b : v)
        if(b)
            return false;
    return true;
}

void test_r_crypto::test_random_bytes()
{
    RTF_ASSERT(random_bytes(0).empty());
    auto one = random_bytes(32);
    auto two = random_bytes(32);
    RTF_ASSERT_EQUAL(one.size(), (size_t)32);
    RTF_ASSERT(one != two);
    RTF_ASSERT(!all_zero(one));
}

void test_r_crypto::test_x25519_rfc7748()
{
    // RFC 7748 section 6.1: each side's public key from its secret, and both get
    // the same shared secret.
    auto a = unhex(RFC7748_ALICE_PRIVATE);
    auto b = unhex(RFC7748_BOB_PRIVATE);
    auto alice = r_x25519_key_pair::from_secret(a.data(), a.size());
    auto bob = r_x25519_key_pair::from_secret(b.data(), b.size());
    RTF_ASSERT(bytes(alice.public_key()) == unhex(RFC7748_ALICE_PUBLIC));
    RTF_ASSERT(bytes(bob.public_key()) == unhex(RFC7748_BOB_PUBLIC));

    array<uint8_t, 32> k1, k2;
    auto bob_public = bob.public_key();
    auto alice_public = alice.public_key();
    RTF_ASSERT(alice.shared_secret(bob_public.data(), bob_public.size(), k1));
    RTF_ASSERT(bob.shared_secret(alice_public.data(), alice_public.size(), k2));
    RTF_ASSERT(bytes(k1) == unhex(RFC7748_ALICE_BOB_SHARED));
    RTF_ASSERT(k1 == k2);
}

void test_r_crypto::test_x25519_wycheproof_vectors()
{
    // Every vector's shared secret, including public keys on the twist, with the
    // top bit set, or not reduced mod p. A low-order public key gives all zeros,
    // which is refused (RFC 7748 section 6.1); Wycheproof allows either.
    size_t checked = 0, refused = 0, wrong = 0;
    for(auto& v : WYCHEPROOF_X25519)
    {
        auto secret = unhex(v.private_key);
        auto peer = unhex(v.public_key);
        auto expected = unhex(v.shared);
        auto pair = r_x25519_key_pair::from_secret(secret.data(), secret.size());
        array<uint8_t, 32> shared {};
        bool ok = pair.shared_secret(peer.data(), peer.size(), shared);
        if(all_zero(expected))
        {
            ++refused;
            if(ok)
            {
                printf("wycheproof x25519 tcId %d (%s): an all-zero secret was given\n", v.tc_id, v.flags);
                ++wrong;
            }
        }
        else
        {
            ++checked;
            if(!ok || bytes(shared) != expected)
            {
                printf("wycheproof x25519 tcId %d (%s): wrong shared secret\n", v.tc_id, v.flags);
                ++wrong;
            }
        }
    }
    RTF_ASSERT_EQUAL(checked + refused, (size_t)518);
    RTF_ASSERT(refused > 0);
    RTF_ASSERT_EQUAL(wrong, (size_t)0);
}

void test_r_crypto::test_x25519_generate()
{
    auto one = r_x25519_key_pair::generate();
    auto two = r_x25519_key_pair::generate();
    RTF_ASSERT(one.public_key() != two.public_key());
    array<uint8_t, 32> k1, k2;
    auto p1 = one.public_key();
    auto p2 = two.public_key();
    RTF_ASSERT(one.shared_secret(p2.data(), p2.size(), k1));
    RTF_ASSERT(two.shared_secret(p1.data(), p1.size(), k2));
    RTF_ASSERT(k1 == k2);
}

void test_r_crypto::test_x25519_sizes()
{
    auto a = unhex(RFC7748_ALICE_PRIVATE);
    RTF_ASSERT_THROWS(r_x25519_key_pair::from_secret(a.data(), 31), r_invalid_argument_exception);
    RTF_ASSERT_THROWS(r_x25519_key_pair::from_secret(nullptr, 32), r_invalid_argument_exception);
    auto alice = r_x25519_key_pair::from_secret(a.data(), a.size());
    auto b = unhex(RFC7748_BOB_PUBLIC);
    array<uint8_t, 32> out {};
    RTF_ASSERT(!alice.shared_secret(b.data(), 31, out));
    b.push_back(0);
    RTF_ASSERT(!alice.shared_secret(b.data(), b.size(), out));
    RTF_ASSERT(!alice.shared_secret(nullptr, 32, out));
}

void test_r_crypto::test_x25519_move_wipes_the_source()
{
    auto a = unhex(RFC7748_ALICE_PRIVATE);
    auto alice = r_x25519_key_pair::from_secret(a.data(), a.size());
    auto pk = alice.public_key();
    auto moved = std::move(alice);
    RTF_ASSERT(moved.public_key() == pk);
    array<uint8_t, 32> zero {};
    RTF_ASSERT(alice.public_key() == zero);
    // A moved-from pair gives no shared secret worth having: its secret is gone.
    auto b = unhex(RFC7748_BOB_PUBLIC);
    array<uint8_t, 32> k {};
    alice.shared_secret(b.data(), b.size(), k);
    RTF_ASSERT(bytes(k) != unhex(RFC7748_ALICE_BOB_SHARED));

    auto other = r_x25519_key_pair::generate();
    other = std::move(moved);
    RTF_ASSERT(other.public_key() == pk);
    RTF_ASSERT(moved.public_key() == zero);
}

void test_r_crypto::test_hkdf_sha512_wycheproof_vectors()
{
    // Every size up to the limit of 255 * 64 bytes, and one byte past it, refused.
    size_t valid = 0, invalid = 0;
    for(auto& v : WYCHEPROOF_HKDF_SHA512)
    {
        auto ikm = unhex(v.ikm);
        auto salt = unhex(v.salt);
        auto info = unhex(v.info);
        if(v.result == WP_VALID)
        {
            auto okm = hkdf_sha512(ikm.data(), ikm.size(), salt.data(), salt.size(), info.data(), info.size(), v.size);
            if(okm != unhex(v.okm))
                printf("wycheproof hkdf tcId %d: wrong output\n", v.tc_id);
            RTF_ASSERT(okm == unhex(v.okm));
            ++valid;
        }
        else
        {
            RTF_ASSERT_THROWS(hkdf_sha512(ikm.data(), ikm.size(), salt.data(), salt.size(), info.data(), info.size(), v.size), r_invalid_argument_exception);
            ++invalid;
        }
    }
    RTF_ASSERT_EQUAL(valid, (size_t)80);
    RTF_ASSERT_EQUAL(invalid, (size_t)3);
}

void test_r_crypto::test_chacha20_poly1305_wycheproof_vectors()
{
    // Valid vectors seal to their ciphertext and tag, and open back. A modified
    // tag doesn't open; a nonce that isn't 12 bytes throws.
    size_t valid = 0, invalid = 0;
    for(auto& v : WYCHEPROOF_CHACHA20_POLY1305)
    {
        auto key = unhex(v.key);
        auto nonce = unhex(v.nonce);
        auto aad = unhex(v.aad);
        auto msg = unhex(v.msg);
        auto sealed_expected = unhex(v.ct);
        auto tag = unhex(v.tag);
        sealed_expected.insert(sealed_expected.end(), tag.begin(), tag.end());

        if(nonce.size() != 12)
        {
            RTF_ASSERT(v.result == WP_INVALID);
            // In a buffer of at least 12, so a missing size check reads bytes that
            // are there and the test fails rather than crashes.
            vector<uint8_t> room(max(nonce.size(), (size_t)12), 0);
            copy(nonce.begin(), nonce.end(), room.begin());
            RTF_ASSERT_THROWS(chacha20_poly1305_seal(key.data(), key.size(), room.data(), nonce.size(), aad.data(), aad.size(), msg.data(), msg.size()), r_invalid_argument_exception);
            vector<uint8_t> out;
            RTF_ASSERT_THROWS(chacha20_poly1305_open(key.data(), key.size(), room.data(), nonce.size(), aad.data(), aad.size(), sealed_expected.data(), sealed_expected.size(), out), r_invalid_argument_exception);
            ++invalid;
            continue;
        }

        vector<uint8_t> opened {0xee};
        bool ok = chacha20_poly1305_open(key.data(), key.size(), nonce.data(), nonce.size(), aad.data(), aad.size(), sealed_expected.data(), sealed_expected.size(), opened);
        if(v.result == WP_VALID)
        {
            auto sealed = chacha20_poly1305_seal(key.data(), key.size(), nonce.data(), nonce.size(), aad.data(), aad.size(), msg.data(), msg.size());
            if(sealed != sealed_expected || !ok || opened != msg)
                printf("wycheproof chacha20-poly1305 tcId %d (%s): wrong\n", v.tc_id, v.flags);
            RTF_ASSERT(sealed == sealed_expected);
            RTF_ASSERT(ok);
            RTF_ASSERT(opened == msg);
            ++valid;
        }
        else
        {
            if(ok)
                printf("wycheproof chacha20-poly1305 tcId %d (%s): opened\n", v.tc_id, v.flags);
            RTF_ASSERT(!ok);
            RTF_ASSERT(opened.empty());
            ++invalid;
        }
    }
    RTF_ASSERT_EQUAL(valid, (size_t)256);
    RTF_ASSERT_EQUAL(invalid, (size_t)69);
}

void test_r_crypto::test_chacha20_poly1305_tampering()
{
    // Any change to the ciphertext, the tag, the additional data, the nonce or the
    // key is caught; less than a tag never opens; a key that isn't 32 bytes throws.
    vector<uint8_t> key(32, 0x42), nonce(12, 0x07), aad = {1, 2, 3};
    string text = "the camera at the back door";
    vector<uint8_t> msg(text.begin(), text.end());
    auto sealed = chacha20_poly1305_seal(key.data(), key.size(), nonce.data(), nonce.size(), aad.data(), aad.size(), msg.data(), msg.size());
    RTF_ASSERT_EQUAL(sealed.size(), msg.size() + 16);

    auto opens = [&](const vector<uint8_t>& k, const vector<uint8_t>& n, const vector<uint8_t>& a, const vector<uint8_t>& s) {
        vector<uint8_t> out;
        bool ok = chacha20_poly1305_open(k.data(), k.size(), n.data(), n.size(), a.data(), a.size(), s.data(), s.size(), out);
        return ok && out == msg;
    };
    RTF_ASSERT(opens(key, nonce, aad, sealed));

    auto flip = [](vector<uint8_t> v, size_t i) { v[i] ^= 0x01; return v; };
    RTF_ASSERT(!opens(key, nonce, aad, flip(sealed, 0)));
    RTF_ASSERT(!opens(key, nonce, aad, flip(sealed, sealed.size() - 1)));
    RTF_ASSERT(!opens(key, nonce, flip(aad, 1), sealed));
    RTF_ASSERT(!opens(key, flip(nonce, 11), aad, sealed));
    RTF_ASSERT(!opens(flip(key, 31), nonce, aad, sealed));
    RTF_ASSERT(!opens(key, nonce, aad, vector<uint8_t>(sealed.begin(), sealed.begin() + 15)));

    vector<uint8_t> out;
    RTF_ASSERT_THROWS(chacha20_poly1305_open(key.data(), 31, nonce.data(), nonce.size(), aad.data(), aad.size(), sealed.data(), sealed.size(), out), r_invalid_argument_exception);
    RTF_ASSERT_THROWS(chacha20_poly1305_seal(key.data(), 33, nonce.data(), nonce.size(), aad.data(), aad.size(), msg.data(), msg.size()), r_invalid_argument_exception);
}
