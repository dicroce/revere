
#ifndef r_crypto_r_chacha20_poly1305_h
#define r_crypto_r_chacha20_poly1305_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace r_crypto
{

// ChaCha20-Poly1305 authenticated encryption (RFC 8439), on mbedtls: a 32-byte
// key and a 12-byte nonce, which must never be used twice with the same key.
// A key or nonce of another size throws r_invalid_argument_exception.

// The ciphertext, then its 16-byte tag.
R_API std::vector<uint8_t> chacha20_poly1305_seal(
    const uint8_t* key,
    size_t key_size,
    const uint8_t* nonce,
    size_t nonce_size,
    const uint8_t* aad,
    size_t aad_size,
    const uint8_t* plaintext,
    size_t plaintext_size
);

// Opens what seal() made. False, with the plaintext left empty, if the tag
// doesn't match the ciphertext, the additional data, the key and the nonce, or
// if there's less than a tag.
R_API bool chacha20_poly1305_open(
    const uint8_t* key,
    size_t key_size,
    const uint8_t* nonce,
    size_t nonce_size,
    const uint8_t* aad,
    size_t aad_size,
    const uint8_t* sealed,
    size_t sealed_size,
    std::vector<uint8_t>& plaintext
);

}

#endif
