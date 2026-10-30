#include "r_utils/r_tls_fingerprint.h"
#include "r_utils/r_exception.h"

extern "C" {
#include <mbedtls/sha256.h>
}

using namespace std;

string r_utils::tls_fingerprint_sha256(const uint8_t* der, size_t len)
{
    unsigned char hash[32];
    if(mbedtls_sha256(der, len, hash, 0) != 0)
        R_THROW(("mbedtls_sha256() failed."));

    static const char* hex = "0123456789ABCDEF";
    string out;
    out.reserve(32 * 3);
    for(size_t i = 0; i < 32; ++i)
    {
        if(i > 0)
            out.push_back(':');
        out.push_back(hex[hash[i] >> 4]);
        out.push_back(hex[hash[i] & 0x0f]);
    }
    return out;
}
