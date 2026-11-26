#include "r_storage/r_write_delay.h"
#include <atomic>

using namespace std;

namespace
{
atomic<int64_t> _max_ms {-1};
}

void r_storage::note_write_delay(int64_t ms)
{
    if(ms < 0)
        ms = 0;
    auto seen = _max_ms.load(memory_order_relaxed);
    while(ms > seen && !_max_ms.compare_exchange_weak(seen, ms, memory_order_relaxed))
        ;
}

int64_t r_storage::take_write_delay_max()
{
    return _max_ms.exchange(-1, memory_order_relaxed);
}
