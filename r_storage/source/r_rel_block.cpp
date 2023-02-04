
#include "r_storage/r_rel_block.h"

using namespace r_storage;

uint8_t* r_rel_block::append(uint8_t* dst, const uint8_t* src, size_t size, int64_t ts, uint8_t flags)
{
    *(int64_t*)dst = ts;
    dst += sizeof(int64_t);

    *dst = flags;
    dst += sizeof(uint8_t);

    *(uint32_t*)dst = (uint32_t)size;
    dst += sizeof(uint32_t);

    memcpy(dst, src, size);

    return dst + size;
}
