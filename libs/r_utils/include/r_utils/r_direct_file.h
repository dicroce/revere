
#ifndef r_utils_r_direct_file_h
#define r_utils_r_direct_file_h

#include "r_utils/r_macro.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace r_utils
{

// A new file written around the OS's cache, so a write's time is the disk's:
// Windows FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, Linux O_DIRECT.
// macOS has no such flag; there writes are cached and sync() waits for them.
//
// Each write must be a multiple of ALIGNMENT bytes, from memory aligned to it
// (aligned_buffer()).
class r_direct_file final
{
public:
    static constexpr size_t ALIGNMENT = 4096;

    using buffer = std::unique_ptr<uint8_t, void(*)(void*)>;

    // size bytes aligned to ALIGNMENT. Throws if it can't allocate.
    R_API static buffer aligned_buffer(size_t size);

    // Creates path, replacing any file there. Throws if it can't.
    R_API explicit r_direct_file(const std::string& path);
    r_direct_file(const r_direct_file&) = delete;
    r_direct_file& operator=(const r_direct_file&) = delete;
    R_API ~r_direct_file() noexcept;

    // Writes all of data. Throws if it can't.
    R_API void write(const uint8_t* data, size_t len);

    // Returns once what's written is on the disk.
    R_API void sync();

    R_API void close() noexcept;

private:
#ifdef IS_WINDOWS
    void* _h {nullptr};
#else
    int _fd {-1};
#endif
};

}

#endif
