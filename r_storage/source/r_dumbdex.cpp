
#include "r_storage/r_dumbdex.h"
#include "r_utils/r_algorithms.h"
#include "r_utils/r_memory_map.h"
#include "r_utils/r_macro.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <memory>
#include <algorithm>
#include <stdexcept>

using namespace std;
using namespace r_utils;

namespace r_storage
{

static bool _is_power_of_2(uint32_t size)
{
    if(size == 0)
        return false;
    
    return (std::ceil(std::log2(size)) == std::floor(std::log2(size)));
}

r_dumbdex::r_dumbdex() :
    _name(),
    _block(nullptr),
    _max_indexes(0),
    _n_indexes(nullptr),
    _index(nullptr),
    _n_free(nullptr),
    _freedex(nullptr)
{
}

r_dumbdex::r_dumbdex(const std::string& name, uint8_t* p, size_t max_indexes) :
    _name(name),
    _block(p),
    _max_indexes(max_indexes),
    _n_indexes((uint32_t*)&_block[0]),
    _index(&_block[sizeof(uint32_t)]),
    _n_free((uint32_t*)&_block[sizeof(uint32_t) + (_max_indexes * INDEX_ELEMENT_SIZE)]),
    _freedex((uint16_t*)&_block[sizeof(uint32_t) + (_max_indexes * INDEX_ELEMENT_SIZE) + sizeof(uint32_t)])
{
}

r_dumbdex::r_dumbdex(r_dumbdex&& other) noexcept :
    _name(std::move(other._name)),
    _block(std::move(other._block)),
    _max_indexes(std::move(other._max_indexes)),
    _n_indexes(std::move(other._n_indexes)),
    _index(std::move(other._index)),
    _n_free(std::move(other._n_free)),
    _freedex(std::move(other._freedex))
{
    other._block = nullptr;
    other._max_indexes = 0;
    other._n_indexes = nullptr;
    other._index = nullptr;
    other._n_free = nullptr;
    other._freedex = nullptr;
}

r_dumbdex::~r_dumbdex()
{
}

r_dumbdex& r_dumbdex::operator=(r_dumbdex&& other) noexcept
{
    if(this != &other)
    {
        _name = std::move(other._name);
        other._name.clear();
        _block = std::move(other._block);
        other._block = nullptr;
        _max_indexes = std::move(other._max_indexes);
        other._max_indexes = 0;
        _n_indexes = std::move(other._n_indexes);
        other._n_indexes = nullptr;
        _index = std::move(other._index);
        other._index = nullptr;
        _n_free = std::move(other._n_free);
        other._n_free = nullptr;
        _freedex = std::move(other._freedex);
        other._freedex = nullptr;
    }

    return *this;
}

r_dumbdex::iterator r_dumbdex::find_lower_bound(uint64_t ts) const
{
    if(*_n_indexes == 0)
        return r_dumbdex::end();

    auto b = _index;
    auto e = b + (*_n_indexes * INDEX_ELEMENT_SIZE);

    vector<uint8_t> ts_bytes(INDEX_ELEMENT_SIZE);
    memcpy(ts_bytes.data(), &ts, sizeof(uint64_t));

    auto found = lower_bound_bytes(
        b, 
        e, 
        ts_bytes.data(),
        INDEX_ELEMENT_SIZE,
        [this](const uint8_t* p, const uint8_t* ts){
            auto pv = _read_index(p).first;
            return (pv < *(uint64_t*)ts)?-1:(pv > *(uint64_t*)ts)?1:0;
        }
    );

    return r_dumbdex::iterator(this, found);
}

uint16_t r_dumbdex::insert(uint64_t ts)
{
    uint16_t blk = 0;

    if(_needs_rollback())
        _rollback_changes(_block);

    auto f = _open_journal();

    try
    {       
        auto b = _index;
        auto e = b + (*_n_indexes * INDEX_ELEMENT_SIZE);
        auto found = e;
        if(*_n_indexes > 0)
        {
            found = lower_bound_bytes(
                b,
                e,
                (uint8_t*)&ts,
                INDEX_ELEMENT_SIZE,
                [this](const uint8_t* p, const uint8_t* ts){
                    auto pv = _read_index(p).first;
                    return (pv < *(uint64_t*)ts)?-1:(pv > *(uint64_t*)ts)?1:0;
                }
            );
        }

        int insert_index = -1;
        if(*_n_indexes < _max_indexes)
        {
            // We have not yet filled up our index... so we have space here.

            blk = _freedex[*_n_free-1];

            _append_change(f, _block, (uint8_t*)_n_free, sizeof(uint32_t));
            _append_change(f, _block, (uint8_t*)_n_indexes, sizeof(uint32_t));

            // The two cases here are finding a location at the end (which does not require a memmove) or finding a location
            // in the middle (which does require a memmove).
            if(found == e)
            {
                insert_index = *_n_indexes;
                _append_change(f, _block, b + (insert_index * INDEX_ELEMENT_SIZE), INDEX_ELEMENT_SIZE);
                //fsync(fileno(f));
                fflush(f);
                _write_index(b + (insert_index * INDEX_ELEMENT_SIZE), ts, blk);
            }
            else
            {
                insert_index = (int)((found - b) / INDEX_ELEMENT_SIZE);
                uint32_t shift_size = (uint32_t)(e - found);
                _append_change(f, _block, &_index[(insert_index+1)*INDEX_ELEMENT_SIZE], shift_size);
                _append_change(f, _block, b + (insert_index * INDEX_ELEMENT_SIZE), INDEX_ELEMENT_SIZE);
                //fsync(fileno(f));
                fflush(f);
                memmove(&_index[(insert_index+1)*INDEX_ELEMENT_SIZE], &_index[insert_index * INDEX_ELEMENT_SIZE], shift_size);
                _write_index(b + (insert_index * INDEX_ELEMENT_SIZE), ts, blk);
            }

            --(*_n_free);
            ++(*_n_indexes);
        }
        else
        {
            blk = *(uint16_t*)&_index[sizeof(uint64_t)];

            if(found == b || found == (b + INDEX_ELEMENT_SIZE))
            {
                insert_index = 0;
                _append_change(f, _block, b + (insert_index * INDEX_ELEMENT_SIZE), INDEX_ELEMENT_SIZE);
                //fsync(fileno(f));
                fflush(f);
                _write_index(b + (insert_index * INDEX_ELEMENT_SIZE), ts, blk);
            }
            else
            {
                insert_index = (int)((found-INDEX_ELEMENT_SIZE) - b) / (int)INDEX_ELEMENT_SIZE;
                uint32_t shift_size = (uint32_t)((found-INDEX_ELEMENT_SIZE) - b);
                _append_change(f, _block, &_index[0], shift_size);
                _append_change(f, _block, b + (insert_index * INDEX_ELEMENT_SIZE), INDEX_ELEMENT_SIZE);
                //fsync(fileno(f));
                fflush(f);
                memmove(&_index[0], &_index[INDEX_ELEMENT_SIZE], shift_size);
                _write_index(b + (insert_index * INDEX_ELEMENT_SIZE), ts, blk);
            }
        }
    }
    catch(const std::exception& e)
    {
        fclose(f);
        throw e;
    }

    _close_journal(f);

    _remove_journal();

    FULL_MEM_BARRIER();

    return blk;
}

void r_dumbdex::remove(uint64_t ts)
{
    if(_needs_rollback())
        _rollback_changes(_block);
    
    auto f = _open_journal();

    try
    {
        auto b = _index;
        auto e = b + (*_n_indexes * INDEX_ELEMENT_SIZE);
        auto found = lower_bound_bytes(
            b, e, (uint8_t*)&ts, INDEX_ELEMENT_SIZE,
            [this](const uint8_t* p, const uint8_t* ts){
                auto pv = _read_index(p).first;
                return (pv < *(uint64_t*)ts)?-1:(pv > *(uint64_t*)ts)?1:0;
            }
        );

        if(found == e)
            throw std::runtime_error("Not found!");

        auto entry = _read_index(found);

        if(entry.first != ts)
            throw std::runtime_error("Not found!");

        _append_change(f, _block, (uint8_t*)&_freedex[*_n_free], FREEDEX_ELEMENT_SIZE);
        _append_change(f, _block, (uint8_t*)_n_free, sizeof(uint32_t));
        _append_change(f, _block, (uint8_t*)_n_indexes, sizeof(uint32_t));

        uint32_t shift_size = (uint32_t)(e - (found + INDEX_ELEMENT_SIZE));
        if(shift_size > 0)
        {
            _append_change(f, _block, found, shift_size);
            //fsync(fileno(f));
            fflush(f);
            memmove(found, found + INDEX_ELEMENT_SIZE, shift_size);
        }
        else fflush(f);
        //else fsync(fileno(f));

        _freedex[*_n_free] = entry.second;
        ++(*_n_free);
        --(*_n_indexes);
    }
    catch(const std::exception& e)
    {
        fclose(f);
        throw e;
    }

    _close_journal(f);

    _remove_journal();

    FULL_MEM_BARRIER();
}

void r_dumbdex::print()
{
    printf("index = [");
    for(uint32_t i = 0; i < *_n_indexes; ++i)
    {
        auto p = _read_index(&_index[i*INDEX_ELEMENT_SIZE]);
#ifdef IS_WINDOWS
        printf("{%llu, %u} ", p.first, p.second);
#endif
#ifdef IS_LINUX
        printf("{%lu, %u} ", p.first, p.second);
#endif
    }
    printf("]\n");

    printf("freedex = [");
    for(uint32_t i = 0; i < *_n_free; ++i)
        printf("%u, ", ((uint16_t*)_freedex)[i]);
    printf("]\n");
}

void r_dumbdex::allocate(const std::string& name, uint8_t* p, size_t block_size, size_t num_blocks)
{
    memset(p, 0, block_size);
    auto max_indexes = r_dumbdex::max_indexes_within(block_size);
    if(num_blocks > max_indexes)
        R_THROW(("Not enough indexes for number of blocks!"));
    r_dumbdex dumb(name, p, num_blocks);
    // Why do we push the free blocks in backwards? To maintain compatibility with
    // the "consistency" unit test.
    for(uint16_t blk = (uint16_t)num_blocks; blk > 0; --blk)
        dumb._push_free_block(blk);
}

uint32_t r_dumbdex::max_indexes_within(size_t block_size)
{ 
    // 8 here refers to the size of the unint32_t's in front of the 2 indexes
    // reserved is a way for the user to reserve space in the header block for
    // other data.
    auto total_index_size = block_size - 8;

    // The freedex is 5 times smaller than the index
    auto freedex_size = (size_t)(total_index_size * (1./6));
    auto index_size = block_size - freedex_size;

    auto max_in_index = index_size / INDEX_ELEMENT_SIZE;
    auto max_in_freedex = freedex_size / FREEDEX_ELEMENT_SIZE;
    return (max_in_index < max_in_freedex)?(uint32_t)max_in_index:(uint32_t)max_in_freedex;
}

void r_dumbdex::_push_free_block(uint16_t blk)
{
    _freedex[*_n_free] = blk;
    ++(*_n_free);
}

bool r_dumbdex::_verify_index_state(vector<pair<uint64_t, uint16_t>> cmp)
{
    if(*_n_indexes != cmp.size())
        return false;
    auto idx = _index;
    for(auto& cmp_p : cmp)
    {
        auto p = _read_index(idx);
        idx += INDEX_ELEMENT_SIZE;
        if(cmp_p.first != p.first || cmp_p.second != p.second)
            return false;
    }
    return true;
}

void r_dumbdex::_write_index(uint8_t* p, uint64_t ts, uint16_t blk)
{
    *((uint64_t*)p) = ts;
    p += sizeof(uint64_t);
    *((uint16_t*)p) = blk;
}

pair<uint64_t, uint16_t> r_dumbdex::_read_index(const uint8_t* p) const
{
    uint64_t ts = *((uint64_t*)p);
    p += sizeof(uint64_t);
    return make_pair(ts, *((uint16_t*)p));
}

uint32_t r_dumbdex::_num_indexes() const
{
    return *_n_indexes;
}

string r_dumbdex::_get_journal_name() const
{
    auto p = _name.rfind('.');

    return (p==string::npos)?_name + "_dumbdex_journal":_name.substr(0,p) + "_dumbdex_journal";
}

FILE* r_dumbdex::_open_journal()
{
    string file_name = _get_journal_name();
#ifdef IS_WINDOWS
    FILE* f = nullptr;
    fopen_s(&f, file_name.c_str(), "a+");
#endif
#ifdef IS_LINUX
    FILE* f = fopen(file_name.c_str(), "a+");
#endif
    if(!f)
        throw runtime_error("Unable to append to jouranl.");

    return f;
}

void r_dumbdex::_close_journal(FILE* f)
{
    if(f)
        fclose(f);
}

void r_dumbdex::_remove_journal()
{
    string file_name = _get_journal_name();
    ::remove(file_name.c_str());
}

void r_dumbdex::_append_change(FILE* f, const uint8_t* base, const uint8_t* p, size_t size)
{
    uint32_t sz = (uint32_t)size;
    fwrite(&sz, 1, sizeof(uint32_t), f);

    uint32_t offset = (uint32_t)(p - base);
    fwrite(&offset, 1, sizeof(uint32_t), f);

    fwrite(p, 1, size, f);
}

void r_dumbdex::_rollback_changes(uint8_t* base)
{
    auto f = _open_journal();

    try
    {
        while(!feof(f))
        {
            uint32_t size = 0;
            fread(&size, 1, sizeof(uint32_t), f);

            uint32_t offset = 0;
            fread(&offset, 1, sizeof(uint32_t), f);

            uint8_t* dst = base + offset;
            fread(dst, 1, size, f);
        }

        fclose(f);
    }
    catch(const std::exception& e)
    {
        fclose(f);
        throw e;
    }

    _remove_journal();
}

bool r_dumbdex::_needs_rollback()
{
    string file_name = _get_journal_name();
#ifdef IS_WINDOWS
    FILE* f = nullptr;
    fopen_s(&f, file_name.c_str(), "r");
#endif
#ifdef IS_LINUX
    FILE* f = fopen(file_name.c_str(), "r");
#endif
  
    if(f)
    {
        fclose(f);
        return true;
    }
    return false;
}

}
