#include "r_utils/r_file.h"
#include "r_utils/r_memory_map.h"
#include "r_utils/r_exception.h"

#ifdef IS_WINDOWS
#include <io.h>
#else
#include <sys/mman.h>
#endif

using namespace r_utils;

static const uint32_t MAX_MAPPING_LEN = 1048576000;

r_memory_map::r_memory_map() :
#ifdef IS_WINDOWS
    _fileHandle(INVALID_HANDLE_VALUE),
    _mapHandle(INVALID_HANDLE_VALUE),
#endif
    _mem(nullptr),
    _length(0)
{
}

r_memory_map::r_memory_map(
    int fd,
    int64_t offset,
    uint32_t len,
    uint32_t prot,
    uint32_t flags
) :
#ifdef IS_WINDOWS
    _fileHandle( INVALID_HANDLE_VALUE ),
    _mapHandle( INVALID_HANDLE_VALUE ),
#endif
    _mem( NULL ),
    _length( len )
{
    if( fd <= 0 )
        R_THROW(( "Attempting to memory map a bad file descriptor." ));

    if( (len == 0) || (len > MAX_MAPPING_LEN) )
        R_THROW(( "Attempting to memory map more than 1gb is invalid." ));

    if( !(flags & RMM_TYPE_FILE) && !(flags & RMM_TYPE_ANON) )
        R_THROW(( "A mapping must be either a file mapping, or an anonymous mapping (neither was specified)." ));

    if( flags & RMM_FIXED )
        R_THROW(( "r_memory_map does not support fixed mappings." ));

#ifdef IS_WINDOWS
    int protFlags = _GetWinProtFlags( prot );
    int accessFlags = _GetWinAccessFlags( prot );

    if( fd != -1 )
        _fileHandle = (HANDLE)_get_osfhandle( fd );

    if( _fileHandle == INVALID_HANDLE_VALUE )
    {
        if( !(flags & RMM_TYPE_ANON) )
            R_THROW(( "An invalid fd was passed and this is not an anonymous mapping." ));
    }
    else
    {
        if( !DuplicateHandle( GetCurrentProcess(),
                              _fileHandle,
                              GetCurrentProcess(),
                              &_fileHandle,
                              0,
                              FALSE,
                              DUPLICATE_SAME_ACCESS ) )
            R_THROW(( "Unable to duplicate the provided fd file handle." ));

        _mapHandle = CreateFileMapping( _fileHandle, NULL, protFlags, 0, 0, NULL );
        if( _mapHandle == 0 )
            R_THROW(( "Unable to create file mapping"));

        uint64_t ofs = (uint64_t)offset;

        _mem = MapViewOfFile( _mapHandle, accessFlags, (DWORD)(ofs>>32), (DWORD)(ofs&0x00000000FFFFFFFF), len );
        if( _mem == NULL )
        {
            DWORD lastError = GetLastError();
            R_THROW(( "Unable to complete file mapping"));
        }
    }
#else
    _mem = mmap( NULL,
                 _length,
                 _GetPosixProtFlags( prot ),
                 _GetPosixAccessFlags( flags ),
                 fd,
                 offset );

    if(_mem == MAP_FAILED)
        R_THROW(( "Unable to complete file mapping"));
#endif
}

r_memory_map::~r_memory_map() noexcept
{
    _clear();
}

void r_memory_map::advise(int advice, void* addr, size_t length) const
{
#ifndef IS_WINDOWS
    int posixAdvice = _GetPosixAdvice( advice );

    int err = madvise( (addr)?addr:_mem, (length>0)?length:_length, posixAdvice );

    if( err != 0 )
        R_THROW(( "Unable to apply memory mapping advice." ));
#endif
}

void r_memory_map::flush(void* addr, size_t length, bool now)
{
#ifndef IS_WINDOWS
    int err = msync( (addr)?addr:_mem, (length>0)?length:_length, (now) ? MS_SYNC : MS_ASYNC );

    if( err != 0 )
        R_THROW(("Unable to sync memory mapped file."));
#else
    if( !FlushViewOfFile( (addr)?addr:_mem, (length>0)?length:_length ) )
        R_THROW(("Unable to sync memory mapped file."));

    if( now )
    {
        if( !FlushFileBuffers( _fileHandle ) )
            R_THROW(("Unable to flush file handle."));
    }
#endif
}

void r_memory_map::_clear() noexcept
{
#ifdef IS_WINDOWS
    if(_mem != nullptr)
        UnmapViewOfFile( _mem );
    if(_mapHandle != INVALID_HANDLE_VALUE)
        CloseHandle( _mapHandle );
    if(_fileHandle != INVALID_HANDLE_VALUE)
        CloseHandle( _fileHandle );
#else
    if(_mem != nullptr)
        munmap( _mem, _length );
#endif
}

#ifdef IS_WINDOWS

int r_memory_map::_GetWinProtFlags( int flags ) const
{
    int prot = 0;

    if( flags & RMM_PROT_READ )
    {
        if( flags & RMM_PROT_WRITE )
            prot = (flags & RMM_PROT_EXEC) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
        else prot = (flags & RMM_PROT_EXEC) ? PAGE_EXECUTE_READ : PAGE_READONLY;
    }
    else if( flags & RMM_PROT_WRITE )
        prot = (flags & RMM_PROT_EXEC) ? PAGE_EXECUTE_READ : PAGE_WRITECOPY;
    else if( flags & RMM_PROT_EXEC )
        prot = PAGE_EXECUTE_READ;

    return prot;
}

int r_memory_map::_GetWinAccessFlags( int flags ) const
{
    int access = 0;

    if( flags & RMM_PROT_READ )
    {
        if( flags & RMM_PROT_WRITE )
            access = FILE_MAP_WRITE;
        else access = (flags & RMM_PROT_EXEC) ? FILE_MAP_EXECUTE : FILE_MAP_READ;
    }
    else if( flags & RMM_PROT_WRITE )
        access = FILE_MAP_COPY;
    else if( flags & RMM_PROT_EXEC )
        access = FILE_MAP_EXECUTE;

    return access;
}

#else

int r_memory_map::_GetPosixProtFlags( int prot ) const
{
    int osProtFlags = 0;

    if( prot & RMM_PROT_READ )
        osProtFlags |= PROT_READ;
    if( prot & RMM_PROT_WRITE )
        osProtFlags |= PROT_WRITE;
    if( prot & RMM_PROT_EXEC )
        osProtFlags |= PROT_EXEC;

    return osProtFlags;
}

int r_memory_map::_GetPosixAccessFlags( int flags ) const
{
    int osFlags = 0;

    if( flags & RMM_TYPE_FILE )
        osFlags |= MAP_FILE;
    if( flags & RMM_TYPE_ANON )
        osFlags |= MAP_ANONYMOUS;
    if( flags & RMM_SHARED )
        osFlags |= MAP_SHARED;
    if( flags & RMM_PRIVATE )
        osFlags |= MAP_PRIVATE;
    if( flags & RMM_FIXED )
        osFlags |= MAP_FIXED;

    return osFlags;
}

int r_memory_map::_GetPosixAdvice( int advice ) const
{
    int posixAdvice = 0;

    if( advice & RMM_ADVICE_RANDOM )
        posixAdvice |= MADV_RANDOM;
    if( advice & RMM_ADVICE_SEQUENTIAL )
        posixAdvice |= MADV_SEQUENTIAL;
    if( advice & RMM_ADVICE_WILLNEED )
        posixAdvice |= MADV_WILLNEED;
    if( advice & RMM_ADVICE_DONTNEED )
        posixAdvice |= MADV_DONTNEED;

    return posixAdvice;
}

#endif