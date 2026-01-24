
#include "r_utils/r_secure_store.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_file.h"
#include "r_utils/r_logger.h"
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>

#ifdef IS_WINDOWS
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#pragma comment(lib, "crypt32.lib")
#endif

#if defined(IS_LINUX) || defined(IS_MACOS)
#include <unistd.h>
#include <sys/stat.h>
#include <pwd.h>
#endif

using namespace r_utils;

r_secure_store::r_secure_store()
    : _backend(_detect_backend()),
      _cached_key(),
      _key_loaded(false)
{
}

r_secure_store::~r_secure_store()
{
    // Clear cached key from memory
    if (!_cached_key.empty()) {
        memset(_cached_key.data(), 0, _cached_key.size());
        _cached_key.clear();
    }
}

r_secure_store::backend_type r_secure_store::_detect_backend() const
{
#ifdef IS_WINDOWS
    return backend_type::DPAPI;
#else
    return backend_type::UNIX_FILE;
#endif
}

const char* r_secure_store::get_backend_name() const
{
    switch (_backend) {
        case backend_type::DPAPI:
            return "DPAPI";
        case backend_type::UNIX_FILE:
            return "UNIX_FILE";
        case backend_type::UNAVAILABLE:
            return "UNAVAILABLE";
        default:
            return "UNKNOWN";
    }
}

std::vector<uint8_t> r_secure_store::get_master_key()
{
    if (_key_loaded) {
        return _cached_key;
    }

#ifdef IS_WINDOWS
    _cached_key = _load_key_windows();
#else
    _cached_key = _load_key_unix();
#endif

    _key_loaded = true;
    return _cached_key;
}

bool r_secure_store::has_master_key() const
{
    if (_key_loaded) {
        return true;
    }

    std::string key_path = _get_key_path();
    return r_fs::file_exists(key_path);
}

std::string r_secure_store::_get_key_path() const
{
#ifdef IS_WINDOWS
    // Use %LOCALAPPDATA%\Revere\encryption.key
    char local_app_data[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local_app_data))) {
        return std::string(local_app_data) + "\\Revere\\encryption.key";
    } else {
        R_STHROW(r_internal_exception, ("Unable to determine LOCALAPPDATA path"));
    }
#else
    // Unix: ~/.config/revere/encryption.key or /etc/revere/encryption.key
    // Exception: In Flatpak, use ~/Documents/revere/revere/config/encryption.key
    if (getuid() == 0) {
        // Running as root
        return "/etc/revere/encryption.key";
    } else {
        // Running as user
        const char* home = getenv("HOME");
        if (!home) {
            // Fallback to getpwuid
            struct passwd* pw = getpwuid(getuid());
            if (pw && pw->pw_dir) {
                home = pw->pw_dir;
            } else {
                R_STHROW(r_internal_exception, ("Unable to determine home directory"));
            }
        }

        // Check if running in Flatpak - use Documents directory which is accessible
        const char* flatpak_id = getenv("FLATPAK_ID");
        if (flatpak_id != nullptr) {
            return std::string(home) + "/Documents/revere/revere/config/encryption.key";
        }

        return std::string(home) + "/.config/revere/encryption.key";
    }
#endif
}

std::vector<uint8_t> r_secure_store::_generate_random_key(size_t size) const
{
    std::vector<uint8_t> key(size);

    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char* pers = "revere_secure_store";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        R_STHROW(r_internal_exception, ("Failed to seed random number generator: %d", ret));
    }

    ret = mbedtls_ctr_drbg_random(&ctr_drbg, key.data(), key.size());
    if (ret != 0) {
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        R_STHROW(r_internal_exception, ("Failed to generate random key: %d", ret));
    }

    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return key;
}

std::vector<uint8_t> r_secure_store::_load_key_from_file(const std::string& path) const
{
    try {
        return r_fs::read_file(path);
    } catch (const r_exception& e) {
        R_STHROW(r_internal_exception, ("Failed to read encryption key from %s: %s",
                                        path.c_str(), e.what()));
    }
}

void r_secure_store::_save_key_to_file(const std::string& path, const std::vector<uint8_t>& data) const
{
    // Ensure parent directory exists
    std::string dir, filename;
    r_fs::break_path(path, dir, filename);

    if (!dir.empty() && !r_fs::is_dir(dir)) {
        // Create directory (we'll need to handle this recursively if needed)
        try {
            r_fs::mkdir(dir);
        } catch (const r_exception&) {
            // Directory might already exist or parent doesn't exist
            // For now, we'll try to write anyway and let it fail if needed
        }
    }

    // Write the file
    try {
        r_fs::write_file(data.data(), data.size(), path);
    } catch (const r_exception& e) {
        R_STHROW(r_internal_exception, ("Failed to write encryption key to %s: %s",
                                        path.c_str(), e.what()));
    }

#if defined(IS_LINUX) || defined(IS_MACOS)
    // Set restrictive permissions: 0600 (owner read/write only)
    if (chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) {
        R_LOG_WARNING("Failed to set restrictive permissions on %s: %s",
                      path.c_str(), strerror(errno));
    }
#endif
}

#ifdef IS_WINDOWS

std::vector<uint8_t> r_secure_store::_load_key_windows()
{
    std::string key_path = _get_key_path();

    // Check if encrypted key file exists
    if (r_fs::file_exists(key_path)) {
        // Load encrypted blob
        std::vector<uint8_t> encrypted_blob = _load_key_from_file(key_path);

        // Decrypt using DPAPI
        DATA_BLOB input;
        input.pbData = encrypted_blob.data();
        input.cbData = static_cast<DWORD>(encrypted_blob.size());

        DATA_BLOB output;
        if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL,
                              CRYPTPROTECT_UI_FORBIDDEN, &output)) {
            std::vector<uint8_t> key(output.pbData, output.pbData + output.cbData);
            LocalFree(output.pbData);

            R_LOG_INFO("Loaded master encryption key from %s (DPAPI)", key_path.c_str());
            return key;
        } else {
            DWORD error = GetLastError();
            R_STHROW(r_internal_exception,
                    ("Failed to decrypt master key with DPAPI: error %lu", error));
        }
    }

    // Generate new key
    R_LOG_INFO("Generating new master encryption key at %s (DPAPI)", key_path.c_str());
    std::vector<uint8_t> key = _generate_random_key(32);

    // Encrypt with DPAPI
    DATA_BLOB input;
    input.pbData = key.data();
    input.cbData = static_cast<DWORD>(key.size());

    DATA_BLOB output;
    if (CryptProtectData(&input, L"Revere Master Encryption Key", NULL, NULL, NULL,
                        CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        std::vector<uint8_t> encrypted(output.pbData, output.pbData + output.cbData);
        _save_key_to_file(key_path, encrypted);
        LocalFree(output.pbData);

        R_LOG_INFO("Generated and saved new master key (DPAPI-protected)");
        return key;
    } else {
        DWORD error = GetLastError();
        R_STHROW(r_internal_exception,
                ("Failed to encrypt master key with DPAPI: error %lu", error));
    }
}

#endif // IS_WINDOWS

#if defined(IS_LINUX) || defined(IS_MACOS)

std::vector<uint8_t> r_secure_store::_load_key_unix()
{
    std::string key_path = _get_key_path();

    // Check if key file exists
    if (r_fs::file_exists(key_path)) {
        // Verify permissions are restrictive
        struct stat st;
        if (stat(key_path.c_str(), &st) == 0) {
            mode_t mode = st.st_mode & 0777;
            if (mode != 0600 && mode != 0400) {
                R_LOG_WARNING("Encryption key file %s has permissive mode %o (should be 0600)",
                             key_path.c_str(), mode);
            }
        }

        std::vector<uint8_t> key = _load_key_from_file(key_path);
        R_LOG_INFO("Loaded master encryption key from %s", key_path.c_str());
        return key;
    }

    // Generate new key
    R_LOG_INFO("Generating new master encryption key at %s", key_path.c_str());
    std::vector<uint8_t> key = _generate_random_key(32);

    // Save to file with restrictive permissions
    _save_key_to_file(key_path, key);

    R_LOG_INFO("Generated and saved new master key (0600 permissions)");
    return key;
}

#endif // IS_LINUX || IS_MACOS
