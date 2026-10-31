
#ifndef r_crypto_r_random_h
#define r_crypto_r_random_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace r_crypto
{

// Cryptographically secure random bytes (mbedtls CTR_DRBG, seeded from the
// system's entropy source).
R_API std::vector<uint8_t> random_bytes(size_t size);

}

#endif
