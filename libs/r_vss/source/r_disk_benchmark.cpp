#include "r_vss/r_disk_benchmark.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_file.h"

#ifdef IS_WINDOWS
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

#include <chrono>
#include <vector>
#include <cstring>

using namespace std;
using namespace std::chrono;

namespace r_vss
{

uint64_t benchmark_disk_write_speed(const std::string& directory, uint32_t test_size_mb)
{
    const uint64_t test_size = (uint64_t)test_size_mb * 1024 * 1024;
    const uint32_t block_size = 1024 * 1024; // 1MB blocks

    auto test_file_path = r_utils::r_fs::path_join(directory, "disk_benchmark_test.tmp");

    R_LOG_INFO("Starting disk write benchmark: %u MB to %s", test_size_mb, directory.c_str());

#ifdef IS_WINDOWS
    // Windows: Use unbuffered I/O with FILE_FLAG_NO_BUFFERING and FILE_FLAG_WRITE_THROUGH
    // FILE_FLAG_NO_BUFFERING requires aligned buffers and writes

    HANDLE hFile = CreateFileA(
        test_file_path.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
        NULL
    );

    if(hFile == INVALID_HANDLE_VALUE)
    {
        R_LOG_ERROR("Failed to create benchmark file: %s (error %lu)", test_file_path.c_str(), GetLastError());
        return 0;
    }

    // Allocate aligned buffer (FILE_FLAG_NO_BUFFERING requires sector-aligned buffers)
    void* buffer = _aligned_malloc(block_size, 4096);
    if(!buffer)
    {
        R_LOG_ERROR("Failed to allocate aligned buffer for benchmark");
        CloseHandle(hFile);
        DeleteFileA(test_file_path.c_str());
        return 0;
    }
    memset(buffer, 0xAB, block_size);

    auto start = steady_clock::now();

    uint64_t bytes_written = 0;
    while(bytes_written < test_size)
    {
        DWORD written = 0;
        if(!WriteFile(hFile, buffer, block_size, &written, NULL))
        {
            R_LOG_ERROR("Write failed during benchmark (error %lu)", GetLastError());
            _aligned_free(buffer);
            CloseHandle(hFile);
            DeleteFileA(test_file_path.c_str());
            return 0;
        }
        bytes_written += written;
    }

    auto end = steady_clock::now();

    _aligned_free(buffer);
    CloseHandle(hFile);
    DeleteFileA(test_file_path.c_str());

#else
    // Linux/macOS: Use O_DIRECT for unbuffered I/O (where supported)
    int flags = O_WRONLY | O_CREAT | O_TRUNC;

#ifdef __linux__
    flags |= O_DIRECT;
#endif

    int fd = open(test_file_path.c_str(), flags, 0644);
    if(fd < 0)
    {
        R_LOG_ERROR("Failed to create benchmark file: %s", test_file_path.c_str());
        return 0;
    }

    // Allocate aligned buffer for O_DIRECT
    void* buffer = nullptr;
    if(posix_memalign(&buffer, 4096, block_size) != 0)
    {
        R_LOG_ERROR("Failed to allocate aligned buffer for benchmark");
        close(fd);
        unlink(test_file_path.c_str());
        return 0;
    }
    memset(buffer, 0xAB, block_size);

    auto start = steady_clock::now();

    uint64_t bytes_written = 0;
    while(bytes_written < test_size)
    {
        ssize_t written = write(fd, buffer, block_size);
        if(written < 0)
        {
            R_LOG_ERROR("Write failed during benchmark");
            free(buffer);
            close(fd);
            unlink(test_file_path.c_str());
            return 0;
        }
        bytes_written += written;
    }

    // Ensure data is flushed to disk
    fsync(fd);

    auto end = steady_clock::now();

    free(buffer);
    close(fd);
    unlink(test_file_path.c_str());
#endif

    auto elapsed_ms = duration_cast<milliseconds>(end - start).count();
    if(elapsed_ms == 0)
        elapsed_ms = 1;

    uint64_t bytes_per_second = (bytes_written * 1000) / elapsed_ms;

    R_LOG_INFO("Disk benchmark complete: %llu MB written in %lld ms = %llu MB/s (%llu bytes/sec)",
        (unsigned long long)(bytes_written / (1024*1024)),
        (long long)elapsed_ms,
        (unsigned long long)(bytes_per_second / (1024*1024)),
        (unsigned long long)bytes_per_second);

    return bytes_per_second;
}

}
