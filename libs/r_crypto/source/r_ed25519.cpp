
#include "r_crypto/r_ed25519.h"
#include "r_utils/r_exception.h"
#include <cstring>

extern "C"
{
#include "monocypher.h"
#include "monocypher-ed25519.h"
}

using namespace r_crypto;
using namespace r_utils;
using namespace std;

// Whether a point's 32 bytes are its one canonical encoding (RFC 8032 section
// 5.1.3): y below p = 2^255 - 19, and no sign bit when x is 0, which is when y is
// 1 or p - 1. Monocypher accepts the other encodings on purpose, which would let
// a signer give one signature more than one encoding (Wycheproof tcId 151).
static bool _canonical_point(const uint8_t* e)
{
    bool all_ff = true, all_00 = true;
    for(int i = 1; i < 31; ++i)
    {
        all_ff = all_ff && e[i] == 0xff;
        all_00 = all_00 && e[i] == 0x00;
    }
    bool top_ff = (e[31] & 0x7f) == 0x7f;
    bool sign = (e[31] & 0x80) != 0;

    if(all_ff && top_ff && e[0] >= 0xed)
        return false; // y >= p
    if(sign && all_00 && (e[31] & 0x7f) == 0 && e[0] == 0x01)
        return false; // y = 1, x = 0
    if(sign && all_ff && top_ff && e[0] == 0xec)
        return false; // y = p - 1, x = 0
    return true;
}

r_ed25519_key_pair r_ed25519_key_pair::from_seed(const uint8_t* seed, size_t size)
{
    if(!seed || size != 32)
        R_STHROW(r_invalid_argument_exception, ("An Ed25519 seed is 32 bytes, not %zu.", size));

    // crypto_ed25519_key_pair() wipes the seed it's given, so it gets a copy.
    uint8_t copy[32];
    memcpy(copy, seed, sizeof(copy));

    r_ed25519_key_pair pair;
    uint8_t public_key[32];
    crypto_ed25519_key_pair(pair._secret_key.data(), public_key, copy);
    crypto_wipe(copy, sizeof(copy));
    return pair;
}

r_ed25519_key_pair::r_ed25519_key_pair(r_ed25519_key_pair&& obj) noexcept :
    _secret_key(obj._secret_key)
{
    crypto_wipe(obj._secret_key.data(), obj._secret_key.size());
}

r_ed25519_key_pair::~r_ed25519_key_pair() noexcept
{
    crypto_wipe(_secret_key.data(), _secret_key.size());
}

r_ed25519_key_pair& r_ed25519_key_pair::operator=(r_ed25519_key_pair&& obj) noexcept
{
    if(this != &obj)
    {
        _secret_key = obj._secret_key;
        crypto_wipe(obj._secret_key.data(), obj._secret_key.size());
    }
    return *this;
}

array<uint8_t, 32> r_ed25519_key_pair::public_key() const
{
    array<uint8_t, 32> output;
    memcpy(output.data(), _secret_key.data() + 32, output.size());
    return output;
}

array<uint8_t, 64> r_ed25519_key_pair::sign(const uint8_t* msg, size_t size) const
{
    array<uint8_t, 64> signature;
    crypto_ed25519_sign(signature.data(), _secret_key.data(), msg, size);
    return signature;
}

bool r_crypto::ed25519_verify(
    const uint8_t* public_key,
    size_t public_key_size,
    const uint8_t* msg,
    size_t msg_size,
    const uint8_t* signature,
    size_t signature_size
)
{
    if(!public_key || public_key_size != 32 || !signature || signature_size != 64)
        return false;
    if(!_canonical_point(public_key) || !_canonical_point(signature))
        return false;
    return crypto_ed25519_check(signature, public_key, msg, msg_size) == 0;
}
