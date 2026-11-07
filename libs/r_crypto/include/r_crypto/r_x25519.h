
#ifndef r_crypto_r_x25519_h
#define r_crypto_r_x25519_h

#include "r_utils/r_macro.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace r_crypto
{

// X25519 Diffie-Hellman key agreement (RFC 7748), on Monocypher's crypto_x25519.

class r_x25519_key_pair final
{
public:
    // A new key pair from 32 random bytes.
    R_API static r_x25519_key_pair generate();

    // The key pair for a 32-byte secret. X25519 clamps the secret itself.
    R_API static r_x25519_key_pair from_secret(const uint8_t* secret, size_t size);

    r_x25519_key_pair(const r_x25519_key_pair&) = delete;
    R_API r_x25519_key_pair(r_x25519_key_pair&& obj) noexcept;
    R_API ~r_x25519_key_pair() noexcept;

    r_x25519_key_pair& operator=(const r_x25519_key_pair&) = delete;
    R_API r_x25519_key_pair& operator=(r_x25519_key_pair&& obj) noexcept;

    R_API std::array<uint8_t, 32> public_key() const;

    // The shared secret with the peer's 32-byte public key. False if the key
    // isn't 32 bytes, or if the result is all zeros, which a low-order public key
    // gives whatever our secret (RFC 7748 section 6.1).
    R_API bool shared_secret(const uint8_t* peer_public_key, size_t size, std::array<uint8_t, 32>& output) const;

private:
    r_x25519_key_pair() = default;

    // Wiped on destruction and when moved from.
    std::array<uint8_t, 32> _secret {};
    std::array<uint8_t, 32> _public {};
};

}

#endif
