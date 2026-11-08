
#include "r_crypto/r_x25519.h"
#include "r_crypto/r_random.h"
#include "r_utils/r_exception.h"
#include <cstring>

extern "C"
{
#include "monocypher.h"
}

using namespace r_crypto;
using namespace r_utils;
using namespace std;

r_x25519_key_pair r_x25519_key_pair::generate()
{
    auto secret = random_bytes(32);
    auto pair = from_secret(secret.data(), secret.size());
    crypto_wipe(secret.data(), secret.size());
    return pair;
}

r_x25519_key_pair r_x25519_key_pair::from_secret(const uint8_t* secret, size_t size)
{
    if(!secret || size != 32)
        R_STHROW(r_invalid_argument_exception, ("An X25519 secret is 32 bytes, not %zu.", size));
    r_x25519_key_pair pair;
    memcpy(pair._secret.data(), secret, 32);
    crypto_x25519_public_key(pair._public.data(), pair._secret.data());
    return pair;
}

r_x25519_key_pair::r_x25519_key_pair(r_x25519_key_pair&& obj) noexcept :
    _secret(obj._secret),
    _public(obj._public)
{
    crypto_wipe(obj._secret.data(), obj._secret.size());
    crypto_wipe(obj._public.data(), obj._public.size());
}

r_x25519_key_pair::~r_x25519_key_pair() noexcept
{
    crypto_wipe(_secret.data(), _secret.size());
}

r_x25519_key_pair& r_x25519_key_pair::operator=(r_x25519_key_pair&& obj) noexcept
{
    if(this != &obj)
    {
        _secret = obj._secret;
        _public = obj._public;
        crypto_wipe(obj._secret.data(), obj._secret.size());
        crypto_wipe(obj._public.data(), obj._public.size());
    }
    return *this;
}

array<uint8_t, 32> r_x25519_key_pair::public_key() const
{
    return _public;
}

bool r_x25519_key_pair::shared_secret(const uint8_t* peer_public_key, size_t size, array<uint8_t, 32>& output) const
{
    if(!peer_public_key || size != 32)
        return false;
    array<uint8_t, 32> shared;
    crypto_x25519(shared.data(), _secret.data(), peer_public_key);

    static const uint8_t zero[32] = {};
    if(crypto_verify32(shared.data(), zero) == 0)
    {
        crypto_wipe(shared.data(), shared.size());
        return false;
    }
    output = shared;
    crypto_wipe(shared.data(), shared.size());
    return true;
}
