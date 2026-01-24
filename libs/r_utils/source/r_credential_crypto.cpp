
#include "r_utils/r_credential_crypto.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include <mbedtls/gcm.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <cstring>

using namespace r_utils;

namespace
{

// Generate cryptographically secure random bytes
std::vector<uint8_t> generate_random_bytes(size_t size)
{
    std::vector<uint8_t> output(size);

    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char* pers = "r_credential_crypto";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        R_STHROW(r_internal_exception, ("Failed to seed random generator: %d", ret));
    }

    ret = mbedtls_ctr_drbg_random(&ctr_drbg, output.data(), output.size());
    if (ret != 0) {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        R_STHROW(r_internal_exception, ("Failed to generate random bytes: %d", ret));
    }

    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return output;
}

} // anonymous namespace

std::string r_credential_crypto::encrypt_credential(const std::string& plaintext,
                                                     const std::vector<uint8_t>& master_key)
{
    if (master_key.size() != 32) {
        R_STHROW(r_invalid_argument_exception,
                ("Master key must be 32 bytes, got %zu", master_key.size()));
    }

    if (plaintext.empty()) {
        R_STHROW(r_invalid_argument_exception, ("Plaintext cannot be empty"));
    }

    // Generate random IV (12 bytes is standard for GCM)
    std::vector<uint8_t> iv = generate_random_bytes(12);

    // Initialize GCM context
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    // Set up AES-256-GCM with master key (256 bits = 32 bytes * 8)
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                                  master_key.data(), 256);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        R_STHROW(r_internal_exception, ("Failed to set encryption key: %d", ret));
    }

    // Prepare output buffers
    std::vector<uint8_t> ciphertext(plaintext.size());
    std::vector<uint8_t> tag(16); // GCM authentication tag is 16 bytes

    // Encrypt and authenticate
    ret = mbedtls_gcm_crypt_and_tag(&gcm,
                                     MBEDTLS_GCM_ENCRYPT,
                                     plaintext.size(),
                                     iv.data(), iv.size(),
                                     nullptr, 0, // No additional authenticated data
                                     (const uint8_t*)plaintext.data(),
                                     ciphertext.data(),
                                     16, tag.data());

    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        R_STHROW(r_internal_exception, ("Encryption failed: %d", ret));
    }

    // Build output: IV + ciphertext + tag
    std::vector<uint8_t> output;
    output.reserve(iv.size() + ciphertext.size() + tag.size());
    output.insert(output.end(), iv.begin(), iv.end());
    output.insert(output.end(), ciphertext.begin(), ciphertext.end());
    output.insert(output.end(), tag.begin(), tag.end());

    // Encode as base64 for safe storage in SQLite
    return r_string_utils::to_base64(output.data(), output.size());
}

std::string r_credential_crypto::decrypt_credential(const std::string& encrypted,
                                                     const std::vector<uint8_t>& master_key)
{
    if (master_key.size() != 32) {
        R_STHROW(r_invalid_argument_exception,
                ("Master key must be 32 bytes, got %zu", master_key.size()));
    }

    if (encrypted.empty()) {
        R_STHROW(r_invalid_argument_exception, ("Encrypted data cannot be empty"));
    }

    // Decode from base64
    std::vector<uint8_t> decoded;
    try {
        decoded = r_string_utils::from_base64(encrypted);
    } catch (const r_exception& e) {
        R_STHROW(r_invalid_argument_exception,
                ("Failed to decode base64: %s", e.what()));
    }

    // Minimum size: 12-byte IV + 16-byte tag = 28 bytes
    if (decoded.size() < 28) {
        R_STHROW(r_invalid_argument_exception,
                ("Encrypted data too short: %zu bytes (minimum 28)", decoded.size()));
    }

    // Parse components: [IV:12][ciphertext:N][tag:16]
    size_t iv_size = 12;
    size_t tag_size = 16;
    size_t ciphertext_size = decoded.size() - iv_size - tag_size;

    std::vector<uint8_t> iv(decoded.begin(), decoded.begin() + iv_size);
    std::vector<uint8_t> ciphertext(decoded.begin() + iv_size,
                                    decoded.begin() + iv_size + ciphertext_size);
    std::vector<uint8_t> tag(decoded.end() - tag_size, decoded.end());

    // Initialize GCM context
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    // Set up AES-256-GCM with master key
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                                  master_key.data(), 256);
    if (ret != 0) {
        mbedtls_gcm_free(&gcm);
        R_STHROW(r_internal_exception, ("Failed to set decryption key: %d", ret));
    }

    // Prepare output buffer
    std::vector<uint8_t> plaintext(ciphertext_size);

    // Decrypt and verify authentication tag
    ret = mbedtls_gcm_auth_decrypt(&gcm,
                                    ciphertext_size,
                                    iv.data(), iv.size(),
                                    nullptr, 0, // No additional authenticated data
                                    tag.data(), tag_size,
                                    ciphertext.data(),
                                    plaintext.data());

    mbedtls_gcm_free(&gcm);

    if (ret == MBEDTLS_ERR_GCM_AUTH_FAILED) {
        R_STHROW(r_internal_exception,
                ("Decryption failed: authentication tag verification failed (data may be corrupted or tampered)"));
    } else if (ret != 0) {
        R_STHROW(r_internal_exception, ("Decryption failed: %d", ret));
    }

    // Convert to string and return
    return std::string(plaintext.begin(), plaintext.end());
}
