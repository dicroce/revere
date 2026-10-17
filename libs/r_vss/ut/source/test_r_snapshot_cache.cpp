#include "test_r_snapshot_cache.h"
#include "r_vss/r_snapshot_cache.h"
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std;
using namespace std::chrono;
using namespace r_vss;

REGISTER_TEST_FIXTURE(test_r_snapshot_cache);

// Eight callers ask for the same snapshot while it's being built: one build,
// and all eight get its result.
void test_r_snapshot_cache::test_concurrent_requests_share_one_build()
{
    r_snapshot_cache cache(seconds(5));
    atomic<int> builds {0};
    auto make = [&]() {
        ++builds;
        this_thread::sleep_for(milliseconds(200));
        return vector<uint8_t>{1, 2, 3};
    };

    vector<thread> callers;
    atomic<int> right {0};
    for(int i = 0; i < 8; ++i)
        callers.emplace_back([&](){
            if(cache.get("jpg|cam|1000|640|480", make) == vector<uint8_t>{1, 2, 3})
                ++right;
        });
    for(auto& t : callers)
        t.join();

    RTF_ASSERT(builds == 1);
    RTF_ASSERT(right == 8);
}

void test_r_snapshot_cache::test_reused_within_ttl_then_rebuilt()
{
    r_snapshot_cache cache(milliseconds(200));
    int builds = 0;
    auto make = [&]() { ++builds; return vector<uint8_t>{(uint8_t)builds}; };

    RTF_ASSERT(cache.get("k", make) == vector<uint8_t>{1});
    RTF_ASSERT(cache.get("k", make) == vector<uint8_t>{1});
    RTF_ASSERT(builds == 1);

    this_thread::sleep_for(milliseconds(300));
    RTF_ASSERT(cache.get("k", make) == vector<uint8_t>{2});
    RTF_ASSERT(builds == 2);
}

void test_r_snapshot_cache::test_different_keys_build_separately()
{
    r_snapshot_cache cache(seconds(5));
    int builds = 0;
    auto make = [&]() { ++builds; return vector<uint8_t>{(uint8_t)builds}; };

    RTF_ASSERT(cache.get("jpg|cam|1000|640|480", make) == vector<uint8_t>{1});
    RTF_ASSERT(cache.get("jpg|cam|1000|1600|1600", make) == vector<uint8_t>{2});
    RTF_ASSERT(cache.get("jpg|cam|2000|640|480", make) == vector<uint8_t>{3});
    RTF_ASSERT(builds == 3);
}

// A failed build reaches everyone waiting on it, and the next request tries again.
void test_r_snapshot_cache::test_failure_is_not_kept()
{
    r_snapshot_cache cache(seconds(5));
    atomic<int> builds {0};
    auto failing = [&]() -> vector<uint8_t> {
        ++builds;
        this_thread::sleep_for(milliseconds(200));
        throw runtime_error("decode failed");
    };

    atomic<int> threw {0};
    vector<thread> callers;
    for(int i = 0; i < 4; ++i)
        callers.emplace_back([&](){
            try { cache.get("k", failing); }
            catch(const runtime_error&) { ++threw; }
        });
    for(auto& t : callers)
        t.join();

    RTF_ASSERT(builds == 1);
    RTF_ASSERT(threw == 4);
    RTF_ASSERT(cache.size() == 0);

    RTF_ASSERT(cache.get("k", [&](){ ++builds; return vector<uint8_t>{7}; }) == vector<uint8_t>{7});
    RTF_ASSERT(builds == 2);
}

void test_r_snapshot_cache::test_entries_are_bounded()
{
    r_snapshot_cache cache(seconds(5), 3);
    for(int i = 0; i < 6; ++i)
    {
        cache.get("k" + to_string(i), [&](){ return vector<uint8_t>(1000, (uint8_t)i); });
        this_thread::sleep_for(milliseconds(2));
    }
    // Trimmed on the next request, so at most one over.
    RTF_ASSERT(cache.size() <= 4);

    // The newest are the ones kept.
    int builds = 0;
    cache.get("k5", [&](){ ++builds; return vector<uint8_t>(); });
    RTF_ASSERT(builds == 0);
}
