#ifndef r_utils_r_sha256_h
#define r_utils_r_sha256_h

#include "r_utils/r_macro.h"
#include "mbedtls/sha256.h"
#include <cstdint>
#include <string>
#include <array>

namespace r_utils
{

class r_sha256 final
{
public:
    R_API r_sha256();
    R_API ~r_sha256();

    r_sha256(const r_sha256&) = delete;
    r_sha256(r_sha256&&) = delete;
    r_sha256& operator=(const r_sha256&) = delete;
    r_sha256& operator=(r_sha256&&) = delete;

    R_API void update(const uint8_t* src, size_t size);

    R_API void finalize();

    R_API void get(uint8_t* output);
    R_API std::string get_as_string();

private:
    mbedtls_sha256_context _ctx;
    bool _finalized;
    std::array<uint8_t, 32> _result;  // SHA-256 produces 32 bytes
};

}

#endif
