
#ifndef r_credential_crypto_h
#define r_credential_crypto_h

#include "r_utils/r_macro.h"
#include <string>
#include <vector>
#include <cstdint>

namespace r_utils
{

/// Credential encryption/decryption utilities using AES-256-GCM
///
/// These functions provide secure encryption for camera credentials and other
/// sensitive data. The output format is suitable for storing in SQLite as TEXT.
///
/// Encryption format (base64-encoded):
///   [12-byte IV][ciphertext][16-byte authentication tag]
///
/// Security properties:
///   - AES-256-GCM: Authenticated encryption (confidentiality + integrity)
///   - Random IV per encryption (prevents pattern detection)
///   - Authentication tag prevents tampering
///   - Base64 encoding for safe SQLite storage
///
namespace r_credential_crypto
{

/// Encrypt plaintext credential using master key
///
/// @param plaintext The secret to encrypt (e.g., camera password)
/// @param master_key 32-byte encryption key (from r_secure_store)
/// @return Base64-encoded string suitable for SQLite storage
/// @throws r_exception if encryption fails
///
/// Example:
///   auto master_key = r_secure_store().get_master_key();
///   std::string encrypted = encrypt_credential("secretpassword", master_key);
///   // Store encrypted in database
///
R_API std::string encrypt_credential(const std::string& plaintext,
                                      const std::vector<uint8_t>& master_key);

/// Decrypt credential using master key
///
/// @param encrypted Base64-encoded encrypted credential (from database)
/// @param master_key 32-byte encryption key (from r_secure_store)
/// @return Decrypted plaintext
/// @throws r_exception if decryption fails or authentication fails
///
/// Example:
///   auto master_key = r_secure_store().get_master_key();
///   std::string password = decrypt_credential(encrypted_from_db, master_key);
///   // Use password to connect to camera
///
R_API std::string decrypt_credential(const std::string& encrypted,
                                      const std::vector<uint8_t>& master_key);

} // namespace r_credential_crypto

} // namespace r_utils

#endif
