
#include "r_crypto/r_random.h"
#include "r_utils/r_exception.h"
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <cstring>

using namespace r_crypto;
using namespace r_utils;
using namespace std;

vector<uint8_t> r_crypto::random_bytes(size_t size)
{
    vector<uint8_t> output(size);
    if(size == 0)
        return output;

    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char* pers = "r_crypto";
    int r = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, strlen(pers));
    if(r == 0)
        r = mbedtls_ctr_drbg_random(&ctr_drbg, output.data(), output.size());

    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    if(r != 0)
        R_STHROW(r_internal_exception, ("Random bytes failed: -0x%04x", -r));
    return output;
}
