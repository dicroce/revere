
#include "r_utils/r_sha_256.h"
#include "r_utils/r_exception.h"
#include <string.h>

using namespace r_utils;
using namespace std;

r_sha_256::r_sha_256() :
    _digest(),
    _finalized(false),
    _sha256()
{
    SHA256_Init(&_sha256);
}

r_sha_256::~r_sha_256() noexcept
{
    if(!_finalized)
        if(SHA256_Final(_digest, &_sha256) != 1)
            R_STHROW(r_internal_exception, ("r_sha_256 SHA256_Final() returned an error."));
}

void r_sha_256::update( const uint8_t* src, size_t size )
{
    if(_finalized)
        R_STHROW(r_internal_exception, ("r_sha_256 does not support update() called after finalize."));

    if(SHA256_Update(&_sha256, src, size) != 1)
        R_STHROW(r_internal_exception, ("r_sha_256 SHA256_Update() returned an error."));
}

void r_sha_256::finalize()
{
    if(!_finalized)
    {
        _finalized = true;

        if(SHA256_Final(_digest, &_sha256) != 1)
            R_STHROW(r_internal_exception, ("r_sha_256 SHA256_Final() returned an error."));
    }
}

void r_sha_256::get( uint8_t* output ) // returns 32 byte sha256
{
    if( !_finalized )
        R_STHROW(r_internal_exception, ("Please finalize() your r_sha_256 before calling a get() method."));

    memcpy(output, _digest, 32);
}

string r_sha_256::get_as_string()
{
    if( !_finalized )
        R_STHROW(r_internal_exception, ("Please finalize() your r_sha_256 before calling a get() method."));

    return r_string_utils::format(
        "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
        _digest[0], _digest[1],_digest[2], _digest[3],
        _digest[4], _digest[5], _digest[6], _digest[7],
        _digest[8], _digest[9], _digest[10], _digest[11],
        _digest[12], _digest[13], _digest[14], _digest[15],
        _digest[16], _digest[17],_digest[18], _digest[19],
        _digest[20], _digest[21], _digest[22], _digest[23],
        _digest[24], _digest[25], _digest[26], _digest[27],
        _digest[28], _digest[29], _digest[30], _digest[31]);
}
