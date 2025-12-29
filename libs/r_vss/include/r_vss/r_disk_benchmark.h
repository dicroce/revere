#ifndef __r_vss_r_disk_benchmark_h
#define __r_vss_r_disk_benchmark_h

#include <string>
#include <cstdint>
#include "r_utils/r_macro.h"

namespace r_vss
{

// Benchmarks disk write speed by writing a test file with unbuffered I/O.
// Returns the sustained write speed in bytes per second.
// The test file is deleted after the benchmark completes.
//
// Parameters:
//   directory - The directory to write the test file in (should be the recording directory)
//   test_size_mb - Size of test file in megabytes (default 100MB)
//
// Returns:
//   Sustained write speed in bytes per second, or 0 on failure
R_API uint64_t benchmark_disk_write_speed(const std::string& directory, uint32_t test_size_mb = 100);

}

#endif
