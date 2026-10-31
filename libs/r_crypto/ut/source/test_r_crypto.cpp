#include "test_r_crypto.h"
#include "ed25519_vectors.h"
#include "r_crypto/r_ed25519.h"
#include "r_utils/r_exception.h"

#include <cstdio>
#include <cstring>
#include <string>
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
