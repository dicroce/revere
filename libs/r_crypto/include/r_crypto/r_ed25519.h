
#ifndef r_crypto_r_ed25519_h
#define r_crypto_r_ed25519_h

#include "r_utils/r_macro.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace r_crypto
{

// Ed25519 signatures (RFC 8032), for protocols that need them and that mbedtls
// can't do. Built on Monocypher's optional crypto_ed25519_* functions (see
// libs/monocypher/README.revere).

class r_ed25519_key_pair final
{
public:
    // The key pair for a 32-byte seed (RFC 8032 section 5.1.5): the same seed
    // always gives the same pair. The caller's seed is left as it was.
    R_API static r_ed25519_key_pair from_seed(const uint8_t* seed, size_t size);

    r_ed25519_key_pair(const r_ed25519_key_pair&) = delete;
    R_API r_ed25519_key_pair(r_ed25519_key_pair&& obj) noexcept;
    R_API ~r_ed25519_key_pair() noexcept;

    r_ed25519_key_pair& operator=(const r_ed25519_key_pair&) = delete;
    R_API r_ed25519_key_pair& operator=(r_ed25519_key_pair&& obj) noexcept;

    R_API std::array<uint8_t, 32> public_key() const;

    // The signature of the message. Ed25519 signing is deterministic.
    R_API std::array<uint8_t, 64> sign(const uint8_t* msg, size_t size) const;

private:
    r_ed25519_key_pair() = default;

    // The seed, then the public key (Monocypher's layout). Wiped on destruction.
    std::array<uint8_t, 64> _secret_key {};
};

// Whether the signature is a valid Ed25519 signature of the message under the
// public key. A public key that isn't 32 bytes, or a signature that isn't 64,
// is never valid, and neither is a public key or R that isn't the point's
// canonical encoding (RFC 8032 section 5.1.3).
R_API bool ed25519_verify(
    const uint8_t* public_key,
    size_t public_key_size,
    const uint8_t* msg,
    size_t msg_size,
    const uint8_t* signature,
    size_t signature_size
);

}

#endif
