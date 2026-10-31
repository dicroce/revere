#include "test_r_crypto.h"
#include "ed25519_vectors.h"
#include "srp6a_vectors.h"
#include "r_crypto/r_ed25519.h"
#include "r_crypto/r_srp6a.h"
#include "r_utils/r_exception.h"

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
