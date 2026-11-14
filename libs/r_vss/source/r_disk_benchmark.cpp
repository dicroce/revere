#include "r_vss/r_disk_benchmark.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_file.h"
#include "r_utils/r_direct_file.h"

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

    uint64_t bytes_written = 0;
    steady_clock::time_point start, end;
    try
    {
        // Around the OS's cache, so the time is the disk's.
        auto buffer = r_utils::r_direct_file::aligned_buffer(block_size);
        memset(buffer.get(), 0xAB, block_size);

        r_utils::r_direct_file f(test_file_path);

        start = steady_clock::now();
        while(bytes_written < test_size)
        {
            f.write(buffer.get(), block_size);
            bytes_written += block_size;
        }
        f.sync();
        end = steady_clock::now();
    }
    catch(const std::exception& e)
    {
        R_LOG_ERROR("Disk benchmark failed: %s", e.what());
        try { r_utils::r_fs::remove_file(test_file_path); } catch(...) {}
        return 0;
    }
    try { r_utils::r_fs::remove_file(test_file_path); } catch(...) {}

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
