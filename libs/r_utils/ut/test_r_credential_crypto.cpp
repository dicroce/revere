#include "r_utils/r_credential_crypto.h"
#include "r_utils/r_secure_store.h"
#include "r_utils/r_exception.h"
#include <iostream>
#include <cassert>

using namespace r_utils;

void test_basic_encrypt_decrypt()
{
    std::cout << "=== Test: Basic Encrypt/Decrypt ===" << std::endl;

    try {
        // Get master key
        r_secure_store store;
        auto master_key = store.get_master_key();

        // Test with simple password
        std::string original_password = "MySecretPassword123!";

        // Encrypt
        std::string encrypted = r_credential_crypto::encrypt_credential(original_password, master_key);
        std::cout << "Plaintext: " << original_password << std::endl;
        std::cout << "Encrypted (base64): " << encrypted.substr(0, 40) << "..." << std::endl;
        std::cout << "Encrypted length: " << encrypted.length() << " chars" << std::endl;

        // Verify it's different from plaintext
        assert(encrypted != original_password && "Encrypted should differ from plaintext");

        // Decrypt
        std::string decrypted = r_credential_crypto::decrypt_credential(encrypted, master_key);
        std::cout << "Decrypted: " << decrypted << std::endl;

        // Verify they match
        assert(decrypted == original_password && "Decrypted should match original");

        std::cout << "✓ Basic encrypt/decrypt test passed" << std::endl;
    } catch (const r_exception& e) {
        std::cerr << "✗ Test failed: " << e.what() << std::endl;
        throw;
    }
}

void test_multiple_encryptions_unique()
{
    std::cout << "\n=== Test: Multiple Encryptions Produce Different Ciphertext ===" << std::endl;

    try {
        r_secure_store store;
        auto master_key = store.get_master_key();

        std::string password = "SamePassword";

        // Encrypt same password twice
        std::string encrypted1 = r_credential_crypto::encrypt_credential(password, master_key);
        std::string encrypted2 = r_credential_crypto::encrypt_credential(password, master_key);

        // Should be different (due to random IV)
        assert(encrypted1 != encrypted2 && "Multiple encryptions should produce different ciphertext");

        // But both should decrypt to same plaintext
        std::string decrypted1 = r_credential_crypto::decrypt_credential(encrypted1, master_key);
        std::string decrypted2 = r_credential_crypto::decrypt_credential(encrypted2, master_key);

        assert(decrypted1 == password && "First decryption should match");
        assert(decrypted2 == password && "Second decryption should match");

        std::cout << "✓ Multiple encryptions test passed (random IV working)" << std::endl;
    } catch (const r_exception& e) {
        std::cerr << "✗ Test failed: " << e.what() << std::endl;
        throw;
    }
}

void test_various_credential_types()
{
    std::cout << "\n=== Test: Various Credential Types ===" << std::endl;

    try {
        r_secure_store store;
        auto master_key = store.get_master_key();

        // Test different types of credentials
        std::vector<std::string> test_credentials = {
            "admin",                          // Simple username
            "P@ssw0rd!",                      // Common password
            "super_long_password_with_special_chars_!@#$%^&*()_+-=[]{}|;:,.<>?",
            "unicode_test_café_münchen_日本語", // Unicode characters
            "with\nnewlines\nand\ttabs",     // Special whitespace
            "123456",                         // Numeric
            ""                                // Empty string (should fail)
        };

        for (size_t i = 0; i < test_credentials.size() - 1; ++i) { // Skip empty string
            const auto& credential = test_credentials[i];

            std::string encrypted = r_credential_crypto::encrypt_credential(credential, master_key);
            std::string decrypted = r_credential_crypto::decrypt_credential(encrypted, master_key);

            assert(decrypted == credential && "Credential should round-trip correctly");
            std::cout << "  ✓ Tested: " << credential.substr(0, 30)
                      << (credential.length() > 30 ? "..." : "") << std::endl;
        }

        // Test empty string (should throw)
        try {
            r_credential_crypto::encrypt_credential("", master_key);
            std::cerr << "✗ Empty string should have thrown exception" << std::endl;
            assert(false);
        } catch (const r_invalid_argument_exception&) {
            std::cout << "  ✓ Empty string correctly rejected" << std::endl;
        }

        std::cout << "✓ Various credential types test passed" << std::endl;
    } catch (const r_exception& e) {
        std::cerr << "✗ Test failed: " << e.what() << std::endl;
        throw;
    }
}

void test_wrong_key_fails()
{
    std::cout << "\n=== Test: Wrong Key Fails Authentication ===" << std::endl;

    try {
        r_secure_store store;
        auto master_key = store.get_master_key();

        std::string password = "SecretPassword";
        std::string encrypted = r_credential_crypto::encrypt_credential(password, master_key);

        // Create a different key
        std::vector<uint8_t> wrong_key(32, 0xFF);

        // Try to decrypt with wrong key (should fail authentication)
        try {
            std::string decrypted = r_credential_crypto::decrypt_credential(encrypted, wrong_key);
            std::cerr << "✗ Decryption with wrong key should have failed" << std::endl;
            assert(false);
        } catch (const r_internal_exception& e) {
            std::string error_msg = e.what();
            if (error_msg.find("authentication") != std::string::npos ||
                error_msg.find("auth") != std::string::npos) {
                std::cout << "✓ Decryption correctly failed with authentication error" << std::endl;
            } else {
                std::cerr << "✗ Expected authentication error, got: " << error_msg << std::endl;
                throw;
            }
        }

        std::cout << "✓ Wrong key test passed" << std::endl;
    } catch (const r_exception& e) {
        std::cerr << "✗ Test failed: " << e.what() << std::endl;
        throw;
    }
}

void test_tampered_data_fails()
{
    std::cout << "\n=== Test: Tampered Data Fails Authentication ===" << std::endl;

    try {
        r_secure_store store;
        auto master_key = store.get_master_key();

        std::string password = "SecretPassword";
        std::string encrypted = r_credential_crypto::encrypt_credential(password, master_key);

        // Tamper with the encrypted data (flip a bit in the middle)
        std::string tampered = encrypted;
        size_t mid = tampered.length() / 2;
        tampered[mid] = (tampered[mid] == 'A') ? 'B' : 'A';

        // Try to decrypt tampered data (should fail authentication)
        try {
            std::string decrypted = r_credential_crypto::decrypt_credential(tampered, master_key);
            std::cerr << "✗ Decryption of tampered data should have failed" << std::endl;
            assert(false);
        } catch (const r_exception& e) {
            std::cout << "✓ Tampered data correctly rejected: " << e.what() << std::endl;
        }

        std::cout << "✓ Tampered data test passed" << std::endl;
    } catch (const r_exception& e) {
        std::cerr << "✗ Test failed: " << e.what() << std::endl;
        throw;
    }
}

void test_real_world_example()
{
    std::cout << "\n=== Test: Real-World Camera Credentials ===" << std::endl;

    try {
        // Simulate storing camera credentials
        struct CameraCredentials {
            std::string camera_id;
            std::string username;
            std::string password;
            std::string encrypted_username;
            std::string encrypted_password;
        };

        std::vector<CameraCredentials> cameras = {
            {"cam-001", "admin", "admin123", "", ""},
            {"cam-002", "root", "P@ssw0rd!", "", ""},
            {"cam-003", "user", "camera_pass_2024", "", ""},
        };

        // Get master key once
        r_secure_store store;
        auto master_key = store.get_master_key();

        // Encrypt all credentials
        std::cout << "Encrypting credentials for " << cameras.size() << " cameras..." << std::endl;
        for (auto& cam : cameras) {
            cam.encrypted_username = r_credential_crypto::encrypt_credential(cam.username, master_key);
            cam.encrypted_password = r_credential_crypto::encrypt_credential(cam.password, master_key);
            std::cout << "  Camera " << cam.camera_id << ": encrypted" << std::endl;
        }

        // Simulate storing to database (we just keep in memory)
        // In real code: db.execute("UPDATE cameras SET rtsp_username = ?, rtsp_password = ? WHERE id = ?", ...)

        // Later: Retrieve and decrypt
        std::cout << "Decrypting credentials..." << std::endl;
        for (const auto& cam : cameras) {
            std::string decrypted_username = r_credential_crypto::decrypt_credential(
                cam.encrypted_username, master_key);
            std::string decrypted_password = r_credential_crypto::decrypt_credential(
                cam.encrypted_password, master_key);

            assert(decrypted_username == cam.username && "Username should match");
            assert(decrypted_password == cam.password && "Password should match");

            std::cout << "  Camera " << cam.camera_id << ": "
                      << decrypted_username << " / " << decrypted_password << std::endl;
        }

        std::cout << "✓ Real-world example test passed" << std::endl;
    } catch (const r_exception& e) {
        std::cerr << "✗ Test failed: " << e.what() << std::endl;
        throw;
    }
}

int main(int argc, char* argv[])
{
    std::cout << "r_credential_crypto Test Suite\n" << std::endl;

    try {
        test_basic_encrypt_decrypt();
        test_multiple_encryptions_unique();
        test_various_credential_types();
        test_wrong_key_fails();
        test_tampered_data_fails();
        test_real_world_example();

        std::cout << "\n✓ All tests passed!" << std::endl;
        std::cout << "\nSummary:" << std::endl;
        std::cout << "  - Encryption/decryption works correctly" << std::endl;
        std::cout << "  - Random IV ensures unique ciphertexts" << std::endl;
        std::cout << "  - Various credential types supported" << std::endl;
        std::cout << "  - Authentication prevents tampering" << std::endl;
        std::cout << "  - Wrong keys are detected" << std::endl;
        std::cout << "  - Ready for production use!" << std::endl;

        return 0;

    } catch (...) {
        std::cerr << "\n✗ Tests failed!" << std::endl;
        return 1;
    }
}
