
#ifndef r_crypto_r_hkdf_h
#define r_crypto_r_hkdf_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace r_crypto
{

// HKDF with HMAC-SHA512 (RFC 5869), on mbedtls: size bytes of key material from
// the input key material, a salt and an info string. Any of the three may be
// empty. Throws r_invalid_argument_exception for more than 255 * 64 bytes.
R_API std::vector<uint8_t> hkdf_sha512(
    const uint8_t* ikm,
    size_t ikm_size,
    const uint8_t* salt,
    size_t salt_size,
    const uint8_t* info,
    size_t info_size,
    size_t size
);

}

#endif
