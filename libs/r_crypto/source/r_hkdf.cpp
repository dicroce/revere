
#include "r_crypto/r_hkdf.h"
#include "r_utils/r_exception.h"
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>

using namespace r_crypto;
using namespace r_utils;
using namespace std;

vector<uint8_t> r_crypto::hkdf_sha512(
    const uint8_t* ikm,
    size_t ikm_size,
    const uint8_t* salt,
    size_t salt_size,
    const uint8_t* info,
    size_t info_size,
    size_t size
)
{
    if(size > 255 * 64)
        R_STHROW(r_invalid_argument_exception, ("HKDF-SHA512 gives at most %d bytes, not %zu.", 255 * 64, size));

    vector<uint8_t> okm(size);
    if(size == 0)
        return okm;

    static const uint8_t none = 0;
    int r = mbedtls_hkdf(
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA512),
        salt_size ? salt : nullptr, salt_size,
        ikm_size ? ikm : &none, ikm_size,
        info_size ? info : &none, info_size,
        okm.data(), okm.size()
    );
    if(r != 0)
        R_STHROW(r_internal_exception, ("HKDF-SHA512 failed: -0x%04x", -r));
    return okm;
}
