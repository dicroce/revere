#include "r_utils/r_secure_store.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_exception.h"
#include <iostream>
#include <cassert>

using namespace r_utils;

void test_basic_usage()
{
    std::cout << "=== Test: Basic Usage ===" << std::endl;

    try {
        r_secure_store store;

        std::cout << "Backend: " << store.get_backend_name() << std::endl;

        // Get master key (should generate if doesn't exist)
        auto key = store.get_master_key();

        std::cout << "Master key size: " << key.size() << " bytes" << std::endl;
        assert(key.size() == 32 && "Key should be 32 bytes");

        // Verify has_master_key returns true
        assert(store.has_master_key() && "Should have master key after get_master_key()");

        // Get key again (should return cached version)
        auto key2 = store.get_master_key();
        assert(key == key2 && "Same key should be returned");

        std::cout << "✓ Basic usage test passed" << std::endl;
    } catch (const r_exception& e) {
        std::cerr << "✗ Test failed: " << e.what() << std::endl;
        throw;
    }
}

void test_persistence()
{
    std::cout << "\n=== Test: Persistence ===" << std::endl;

    try {
        std::vector<uint8_t> key1;

        // First instance: get/generate key
        {
            r_secure_store store1;
            key1 = store1.get_master_key();
            std::cout << "Generated key in first instance" << std::endl;
        }

        // Second instance: should load same key
        {
            r_secure_store store2;
            assert(store2.has_master_key() && "Key should exist");

            auto key2 = store2.get_master_key();
            std::cout << "Loaded key in second instance" << std::endl;

            assert(key1 == key2 && "Keys should match across instances");
        }

        std::cout << "✓ Persistence test passed" << std::endl;
    } catch (const r_exception& e) {
        std::cerr << "✗ Test failed: " << e.what() << std::endl;
        throw;
    }
}

void test_multiple_instances()
{
    std::cout << "\n=== Test: Multiple Instances ===" << std::endl;

    try {
        r_secure_store store1;
        r_secure_store store2;

        auto key1 = store1.get_master_key();
        auto key2 = store2.get_master_key();

        assert(key1 == key2 && "Different instances should return same key");

        std::cout << "✓ Multiple instances test passed" << std::endl;
    } catch (const r_exception& e) {
        std::cerr << "✗ Test failed: " << e.what() << std::endl;
        throw;
    }
}

void print_key_location()
{
    std::cout << "\n=== Master Key Location ===" << std::endl;

    try {
        r_secure_store store;
        store.get_master_key(); // Ensure key exists

#ifdef IS_WINDOWS
        char local_app_data[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_app_data))) {
            std::cout << "Location: " << local_app_data << "\\Revere\\encryption.key" << std::endl;
            std::cout << "Protection: Windows DPAPI (encrypted)" << std::endl;
        }
#else
        const char* home = getenv("HOME");
        if (getuid() == 0) {
            std::cout << "Location: /etc/revere/encryption.key" << std::endl;
        } else if (home) {
            std::cout << "Location: " << home << "/.config/revere/encryption.key" << std::endl;
        }
        std::cout << "Protection: File permissions (0600)" << std::endl;
#endif

    } catch (const r_exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[])
{
    std::cout << "r_secure_store Test Suite\n" << std::endl;

    try {
        test_basic_usage();
        test_persistence();
        test_multiple_instances();
        print_key_location();

        std::cout << "\n✓ All tests passed!" << std::endl;
        return 0;

    } catch (...) {
        std::cerr << "\n✗ Tests failed!" << std::endl;
        return 1;
    }
}
