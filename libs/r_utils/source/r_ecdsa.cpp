
#include "r_utils/r_ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"

bool r_utils::ecdsa_p256_sha256_verify(const std::string& public_key_pem,
                                       const uint8_t* data, size_t size,
                                       const uint8_t* sig, size_t sig_size)
{
    if((data == nullptr && size != 0) || sig == nullptr || sig_size == 0)
        return false;

    unsigned char hash[32];
    if(mbedtls_sha256(data, size, hash, 0) != 0)
        return false;

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    bool ok = false;
    // A PEM key's length counts its terminating NUL.
    if(mbedtls_pk_parse_public_key(&pk, reinterpret_cast<const unsigned char*>(public_key_pem.c_str()), public_key_pem.size() + 1) == 0 &&
       mbedtls_pk_get_type(&pk) == MBEDTLS_PK_ECKEY &&
       mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(pk)) == MBEDTLS_ECP_DP_SECP256R1)
    {
        ok = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sig_size) == 0;
    }

    mbedtls_pk_free(&pk);
    return ok;
}
