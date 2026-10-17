#ifndef __revere_snapshot_cache_h
#define __revere_snapshot_cache_h

#include "r_utils/r_macro.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace r_vss
{

// Shares the work of building a snapshot (decode a key frame, encode a JPEG or
// WebP) between callers that ask for the same one. The key names the exact
// input (camera, key frame time, format, size), so the output is the same for
// all of them:
//  - while one is being built, others with the same key wait for it instead of
//    decoding again;
//  - a built one is kept for `ttl` and returned as is;
//  - a failure isn't kept: the waiting callers get the exception, the next
//    caller tries again.
// At most `max_entries` built results are kept (the oldest go first).
class r_snapshot_cache final
{
public:
    R_API explicit r_snapshot_cache(std::chrono::steady_clock::duration ttl, size_t max_entries = 64);
    R_API r_snapshot_cache(const r_snapshot_cache&) = delete;
    R_API r_snapshot_cache& operator=(const r_snapshot_cache&) = delete;

    R_API std::vector<uint8_t> get(const std::string& key, const std::function<std::vector<uint8_t>()>& make);

    // The number of entries held (built or being built); for tests.
    R_API size_t size() const;

private:
    struct entry
    {
        std::shared_future<std::vector<uint8_t>> result;
        bool built {false};
        std::chrono::steady_clock::time_point built_at;
    };

    void _expire(std::chrono::steady_clock::time_point now);

    std::chrono::steady_clock::duration _ttl;
    size_t _max_entries;
    mutable std::mutex _lock;
    std::map<std::string, entry> _entries;
};

}

#endif
