
#include "r_utils/r_sha256.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include <string.h>

using namespace r_utils;
using namespace std;

r_sha256::r_sha256() :
    _ctx(),
    _finalized(false),
    _result()
{
    mbedtls_sha256_init(&_ctx);
    int ret = mbedtls_sha256_starts(&_ctx, 0);  // 0 = SHA-256 (not SHA-224)
    if(ret != 0)
        R_THROW(("Failed to initialize SHA-256 context."));
}

r_sha256::~r_sha256()
{
    mbedtls_sha256_free(&_ctx);
}

void r_sha256::update(const uint8_t* src, size_t size)
{
    if(_finalized)
        R_THROW(("Cannot update SHA-256 after finalization."));

    int ret = mbedtls_sha256_update(&_ctx, src, size);
    if(ret != 0)
        R_THROW(("Failed to update SHA-256."));
}

void r_sha256::finalize()
{
    if(_finalized)
        return;  // Already finalized

    int ret = mbedtls_sha256_finish(&_ctx, &_result[0]);
    if(ret != 0)
        R_THROW(("Failed to finalize SHA-256."));

    _finalized = true;
}

void r_sha256::get(uint8_t* output)
{
    if(!_finalized)
        R_THROW(("Please finalize() sha256 before get()."));

    memcpy(output, &_result[0], 32);
}

string r_sha256::get_as_string()
{
    if(!_finalized)
        R_THROW(("Please finalize() sha256 before get_as_string()."));

    return r_string_utils::format(
        "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
        "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
        _result[0], _result[1], _result[2], _result[3],
        _result[4], _result[5], _result[6], _result[7],
        _result[8], _result[9], _result[10], _result[11],
        _result[12], _result[13], _result[14], _result[15],
        _result[16], _result[17], _result[18], _result[19],
        _result[20], _result[21], _result[22], _result[23],
        _result[24], _result[25], _result[26], _result[27],
        _result[28], _result[29], _result[30], _result[31]
    );
}
