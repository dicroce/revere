
#ifndef r_secure_store_h
#define r_secure_store_h

#include "r_utils/r_macro.h"
#include <vector>
#include <cstdint>
#include <string>

namespace r_utils
{

/// Secure master key storage using OS-native facilities
///
/// Stores a single 32-byte master encryption key that can be used
/// to encrypt/decrypt application secrets (like camera credentials).
///
/// Backend selection (automatic):
///   - Windows: DPAPI (Data Protection API) - Key encrypted and tied to user account
///   - macOS:   File with 0600 permissions in ~/.config/revere/
///   - Linux:   File with 0600 permissions in ~/.config/revere/
///
/// The key is generated automatically on first access and persists
/// across application restarts.
///
/// Thread Safety: This class is NOT thread-safe. Create one instance
/// per thread or protect with external synchronization.
///
class r_secure_store
{
public:
    /// Backend types
    enum class backend_type
    {
        DPAPI,       ///< Windows Data Protection API
        UNIX_FILE,   ///< Secure file with restricted permissions (macOS/Linux)
        UNAVAILABLE  ///< No secure storage available (should never happen)
    };

    R_API r_secure_store();
    R_API ~r_secure_store();

    // Disable copy
    r_secure_store(const r_secure_store&) = delete;
    r_secure_store& operator=(const r_secure_store&) = delete;

    /// Get the master encryption key
    /// - Automatically generates new key on first access
    /// - Caches in memory for performance
    /// @return 32-byte encryption key
    /// @throws r_exception if key cannot be accessed or generated
    R_API std::vector<uint8_t> get_master_key();

    /// Check if master key exists
    /// @return true if master key has been generated and stored
    R_API bool has_master_key() const;

    /// Get active backend type (for diagnostics/logging)
    R_API backend_type get_backend() const { return _backend; }

    /// Get backend name as string (for logging)
    R_API const char* get_backend_name() const;

private:
    backend_type _backend;
    std::vector<uint8_t> _cached_key;
    bool _key_loaded;

    backend_type _detect_backend() const;

    std::vector<uint8_t> _load_key_windows();
    std::vector<uint8_t> _load_key_unix();

    std::string _get_key_path() const;
    std::vector<uint8_t> _generate_random_key(size_t size) const;
    std::vector<uint8_t> _load_key_from_file(const std::string& path) const;
    void _save_key_to_file(const std::string& path, const std::vector<uint8_t>& data) const;
};

} // namespace r_utils

#endif
