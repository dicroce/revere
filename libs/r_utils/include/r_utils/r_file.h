

#ifndef r_utils_r_file_h
#define r_utils_r_file_h

#define _LARGE_FILE_API
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "r_utils/r_string_utils.h"
#include "r_utils/r_exception.h"
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <vector>
#include <future>

class test_r_utils_r_file;

namespace r_utils
{

class r_file final
{
    friend class ::test_r_utils_r_file;
public:
    R_API r_file() : _f(nullptr) {}
    R_API r_file(const r_file&) = delete;
	R_API r_file(r_file&& obj) noexcept;
	R_API ~r_file() noexcept;

    R_API r_file& operator = (r_file&) = delete;
	R_API r_file& operator = (r_file&& obj) noexcept;

	R_API operator FILE*() const { return _f; }

	R_API static r_file open(const std::string& path, const std::string& mode)
    {
#ifdef IS_WINDOWS
#pragma warning(push)
#pragma warning(disable : 4996)
        r_file obj;
        obj._f = _fsopen(path.c_str(), mode.c_str(), _SH_DENYNO);
        if(!obj._f)
            R_STHROW(r_not_found_exception, ("Unable to open: %s",path.c_str()));
        return obj;
#pragma warning(pop)
#endif
#ifdef IS_LINUX
        r_file obj;
        obj._f = fopen(path.c_str(), mode.c_str());
        if(!obj._f)
            R_STHROW(r_not_found_exception, ("Unable to open: %s, (%s)", path.c_str(), strerror(errno)));
        return obj;
#endif
    }

	R_API void close() { if(_f) {fclose(_f); _f = nullptr;} }

private:
    FILE* _f;
};

namespace r_fs
{

#ifdef IS_WINDOWS
#define PATH_SLASH std::string("\\")
#endif
#ifdef IS_LINUX
#define PATH_SLASH std::string("/")
#endif

enum r_file_type
{
    R_REGULAR,
    R_DIRECTORY
};

struct r_file_info
{
    std::string file_name;
    uint64_t file_size;
    r_file_type file_type;
};

R_API std::string current_exe_path();
R_API std::string platform_path(const std::string& path);
R_API std::string working_directory();
R_API void change_working_directory(const std::string& dir);
R_API std::string path_join(const std::string& path, const std::string& filename);
R_API int stat(const std::string& fileName, struct r_file_info* fileInfo);
R_API uint64_t file_size(const std::string& fileName);
R_API int fileno(FILE* f);
R_API std::vector<uint8_t> read_file(const std::string& path);
R_API void write_file(const uint8_t* bytes, size_t len, const std::string& path);
R_API void atomic_rename_file(const std::string& oldPath, const std::string& newPath);
R_API bool file_exists(const std::string& path);
R_API bool is_reg(const std::string& path);
R_API bool is_dir(const std::string& path);
R_API int fallocate(FILE* file, uint64_t size);
R_API void break_path(const std::string& path, std::string& dir, std::string& fileName);
R_API std::string temp_file_name(const std::string& dir, const std::string& baseName = std::string());
R_API void get_fs_usage(const std::string& path, uint64_t& size, uint64_t& free);
R_API void mkdir(const std::string& path);
R_API void rmdir(const std::string& path);
R_API void remove_file(const std::string& path);

// GLIBC fwrite() attempts to write the requested number of bytes and returns the number of items
// successfully written. If GLIBC fwrite() is unable to write any bytes at all the error flag is
// set on the stream. NOTE: It looks like fwrite() can return 0 items written but have actually written
// some data to the file!
template<typename T, typename F>
void block_write_file(const T* p, size_t size, const F& f, size_t blockSize = 4096)
{
    size_t blocks = (size >= blockSize)?size / blockSize:0;
    size_t remainder = (size >= blockSize)?size % blockSize:size;
    const uint8_t* writeHead = (const uint8_t*)p;

    while(blocks > 0)
    {
        auto blocksWritten = fwrite((void*)writeHead, blockSize, blocks, f);
        if(blocksWritten == 0)
            R_THROW(("Unable to write to file."));
        blocks -= blocksWritten;
        writeHead += (blocksWritten * blockSize);
    }

    while(remainder > 0)
    {
        auto bytesWritten = fwrite((void*)writeHead, 1, remainder, f);
        if(bytesWritten == 0)
            R_THROW(("Could not write to file."));
        remainder -= bytesWritten;
        writeHead += bytesWritten;
    }
}

// GLIBC fread() loops until it has read the requested number of bytes OR the lower level __read
// returns 0. If the lower level __read returns 0 the EOF flag is set on the stream.
template<typename T, typename F>
void block_read_file(T* p, size_t size, const F& f, size_t blockSize = 4096)
{
    size_t blocks = (size >= blockSize)?size / blockSize:0;
    size_t remainder = (size >= blockSize)?size % blockSize:size;
    uint8_t* readHead = (uint8_t*)p;

    if(blocks > 0)
    {
        auto blocksRead = fread((void*)readHead, blockSize, blocks, f);
        if(blocksRead != blocks)
            R_THROW(("Short fread(). Implies feof()."));
        readHead += (blocksRead * blockSize);
    }

    if(remainder > 0)
    {
        auto bytesRead = fread((void*)readHead, 1, remainder, f);
        if(bytesRead != remainder)
            R_THROW(("Short fread(). Indicates feof()"));
    }
}

}

}

#endif
