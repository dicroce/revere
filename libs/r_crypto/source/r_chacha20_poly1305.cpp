
#include "r_crypto/r_chacha20_poly1305.h"
#include "r_utils/r_exception.h"
#include <mbedtls/chachapoly.h>
#include <mbedtls/platform_util.h>

using namespace r_crypto;
using namespace r_utils;
using namespace std;

static const size_t TAG_SIZE = 16;

static void _check_sizes(size_t key_size, size_t nonce_size)
{
    if(key_size != 32)
        R_STHROW(r_invalid_argument_exception, ("A ChaCha20-Poly1305 key is 32 bytes, not %zu.", key_size));
    if(nonce_size != 12)
        R_STHROW(r_invalid_argument_exception, ("A ChaCha20-Poly1305 nonce is 12 bytes, not %zu.", nonce_size));
}

vector<uint8_t> r_crypto::chacha20_poly1305_seal(
    const uint8_t* key,
    size_t key_size,
    const uint8_t* nonce,
    size_t nonce_size,
    const uint8_t* aad,
    size_t aad_size,
    const uint8_t* plaintext,
    size_t plaintext_size
)
{
    _check_sizes(key_size, nonce_size);
    vector<uint8_t> sealed(plaintext_size + TAG_SIZE);

    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    int r = mbedtls_chachapoly_setkey(&ctx, key);
    if(r == 0)
        r = mbedtls_chachapoly_encrypt_and_tag(&ctx, plaintext_size, nonce, aad, aad_size, plaintext, sealed.data(), sealed.data() + plaintext_size);
    mbedtls_chachapoly_free(&ctx);
    if(r != 0)
        R_STHROW(r_internal_exception, ("ChaCha20-Poly1305 encryption failed: -0x%04x", -r));
    return sealed;
}

bool r_crypto::chacha20_poly1305_open(
    const uint8_t* key,
    size_t key_size,
    const uint8_t* nonce,
    size_t nonce_size,
    const uint8_t* aad,
    size_t aad_size,
    const uint8_t* sealed,
    size_t sealed_size,
    vector<uint8_t>& plaintext
)
{
    _check_sizes(key_size, nonce_size);
    plaintext.clear();
    if(!sealed || sealed_size < TAG_SIZE)
        return false;

    size_t size = sealed_size - TAG_SIZE;
    vector<uint8_t> output(size);

    mbedtls_chachapoly_context ctx;
    mbedtls_chachapoly_init(&ctx);
    int r = mbedtls_chachapoly_setkey(&ctx, key);
    if(r == 0)
        r = mbedtls_chachapoly_auth_decrypt(&ctx, size, nonce, aad, aad_size, sealed + size, sealed, output.data());
    mbedtls_chachapoly_free(&ctx);
    if(r == MBEDTLS_ERR_CHACHAPOLY_AUTH_FAILED)
    {
        if(!output.empty())
            mbedtls_platform_zeroize(output.data(), output.size());
        return false;
    }
    if(r != 0)
        R_STHROW(r_internal_exception, ("ChaCha20-Poly1305 decryption failed: -0x%04x", -r));
    plaintext = std::move(output);
    return true;
}
