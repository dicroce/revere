#ifndef r_storage_r_rel_block_h_
#define r_storage_r_rel_block_h_

#include "r_utils/r_macro.h"
#include <cstdint>
#include <cstring>
#include <vector>

class test_r_storage;
namespace r_storage
{

struct r_rel_block_info
{
    int64_t ts;
    uint8_t flags;
    const uint8_t* data;
    size_t size;
};

class r_rel_block
{
    friend class ::test_r_storage;
public:
    enum r_rel_block_enum
    {
        PER_FRAME_OVERHEAD = 13
    };
    class iterator
    {
    public:
        R_API iterator(
            const uint8_t* block_start = nullptr,
            size_t size = 0,
            const uint8_t* pos = nullptr
        ) :
            _block_start(block_start),
            _size(size),
            _pos(pos),
            _pos_stack()
        {
            _pos_stack.push_back(_pos);
        }

        R_API r_rel_block_info operator*() const
        {
            r_rel_block_info info;
            info.ts = *(int64_t*)_pos;
            info.flags = *(uint8_t*)(_pos + 8);
            info.size = *(uint32_t*)(_pos + 9);
            info.data = _pos + 13;

            return info;
        }

        R_API bool operator==(const iterator& other) const
        {
            return _pos == other._pos && _block_start == other._block_start && _size == other._size;
        }

        R_API bool next()
        {
            if(_pos < (_block_start + _size))
            {
                _pos += *(uint32_t*)(_pos + 9) + 13;
                _pos_stack.push_back(_pos);
                return true;
            }
            return false;
        };

        R_API bool prev()
        {
            if(_pos_stack.size() > 1)
            {
                _pos_stack.pop_back();
                _pos = _pos_stack.back();
                return true;
            }

            return false;
        }

        R_API bool valid() const
        {
            return _pos < (_block_start + _size);
        }

    private:
        const uint8_t* _block_start;
        size_t _size;
        const uint8_t* _pos;
        std::vector<const uint8_t*> _pos_stack;
    };

    R_API r_rel_block(const uint8_t* data = nullptr, size_t size = 0) :
        _block(data),
        _size(size)
    {
    }

    R_API ~r_rel_block() noexcept {}

    R_API iterator begin() const
    {
        return iterator(_block, _size, _block);
    }

    R_API iterator end() const
    {
        return iterator(_block, _size, _block + _size);
    }

    R_API static uint8_t* append(uint8_t* dst, const uint8_t* src, size_t size, int64_t ts, uint8_t flags);

private:
    const uint8_t* _block;
    size_t _size;
};

}

#endif
