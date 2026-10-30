#ifndef r_utils_r_tls_fingerprint_h
#define r_utils_r_tls_fingerprint_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace r_utils
{

// SHA-256 of a certificate's DER encoding as "AB:CD:...:EF" (95 characters),
// the form browsers and openssl show, so a user can compare them by eye.
R_API std::string tls_fingerprint_sha256(const uint8_t* der, size_t len);

}

#endif
