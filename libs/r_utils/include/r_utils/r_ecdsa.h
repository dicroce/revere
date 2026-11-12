#ifndef r_utils_r_ecdsa_h
#define r_utils_r_ecdsa_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace r_utils
{

// Whether sig is a valid ECDSA P-256 signature, over the SHA-256 of data, by the
// public key in public_key_pem (a PEM "PUBLIC KEY" block). sig is DER encoded,
// as `openssl dgst -sha256 -sign` writes it. A key that doesn't parse, or isn't
// a P-256 EC key, verifies nothing.
R_API bool ecdsa_p256_sha256_verify(const std::string& public_key_pem,
                                    const uint8_t* data, size_t size,
                                    const uint8_t* sig, size_t sig_size);

}

#endif
