
#ifndef __r_utils_r_memory_map_h
#define __r_utils_r_memory_map_h

#include "r_utils/r_string_utils.h"
#include "r_utils/r_byte_ptr.h"
#include "r_utils/r_macro.h"

#ifdef IS_WINDOWS
#include <Windows.h>
#endif

namespace r_utils
{

class r_memory_map
{
public:
    enum Flags
    {
        RMM_TYPE_FILE = 0x01,
        RMM_TYPE_ANON = 0x02,
        RMM_SHARED = 0x04,
        RMM_PRIVATE = 0x08,
        RMM_FIXED = 0x10
    };

    enum Protection
    {
        RMM_PROT_NONE = 0x00,
        RMM_PROT_READ = 0x01,
        RMM_PROT_WRITE = 0x02,
        RMM_PROT_EXEC = 0x04
    };

    enum Advice
    {
        RMM_ADVICE_NORMAL = 0x00,
        RMM_ADVICE_RANDOM = 0x01,
        RMM_ADVICE_SEQUENTIAL = 0x02,
        RMM_ADVICE_WILLNEED = 0x04,
        RMM_ADVICE_DONTNEED = 0x08
    };

    R_API r_memory_map();

    R_API r_memory_map(
        int fd,
        int64_t offset,
        uint32_t len,
        uint32_t prot,
        uint32_t flags
    );
    
    R_API r_memory_map(const r_memory_map&) = delete;

    R_API r_memory_map(r_memory_map&& other) :
#ifdef IS_WINDOWS
        _fileHandle(std::move(other._fileHandle)),
        _mapHandle(std::move(other._mapHandle)),
#endif
        _mem(std::move(other._mem)),
        _length(std::move(other._length))
    {
#ifdef IS_WINDOWS
        other._fileHandle = INVALID_HANDLE_VALUE;
        other._mapHandle = INVALID_HANDLE_VALUE;
#endif
        other._mem = nullptr;
        other._length = 0;
    }

    R_API virtual ~r_memory_map() noexcept;

    R_API r_memory_map& operator=(const r_memory_map& other) = delete;

    R_API r_memory_map& operator=(r_memory_map&& other) noexcept
    {
        if(this != &other)
        {
            _clear();

#ifdef IS_WINDOWS
            _fileHandle = std::move(other._fileHandle);
            other._fileHandle = INVALID_HANDLE_VALUE;
            _mapHandle = std::move(other._mapHandle);
            other._mapHandle = INVALID_HANDLE_VALUE;
#endif
            _mem = std::move(other._mem);
            other._mem = nullptr;
            _length = std::move(other._length);
            other._length = 0;
        }

        return *this;
    }

    R_API inline void* map() const
    {
        return _mem;
    }

    R_API inline uint32_t length() const
    {
        return _length;
    }

    R_API inline bool mapped() const
    {
        return _mem != nullptr;
    }

    R_API void advise(int advice, void* addr = nullptr, size_t length = 0) const;

    R_API void flush(void* addr = nullptr, size_t length = 0, bool now = true);

private:
    R_API void _clear() noexcept;
#ifdef IS_WINDOWS
    int _GetWinProtFlags( int flags ) const;
    int _GetWinAccessFlags( int flags ) const;
#else
    int _GetPosixProtFlags( int prot ) const;
    int _GetPosixAccessFlags( int flags ) const;
    int _GetPosixAdvice( int advice ) const;
#endif

#ifdef IS_WINDOWS
    HANDLE _fileHandle;
    HANDLE _mapHandle;
#endif
    void* _mem;
    uint32_t _length;
};
}

#endif
