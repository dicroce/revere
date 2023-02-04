#ifndef r_storage_r_ind_block_h_
#define r_storage_r_ind_block_h_

#include "r_utils/r_exception.h"
#include "r_utils/r_algorithms.h"
#include "r_utils/r_macro.h"
#include <algorithm>
#include <cstdint>
#include <string>

class test_r_storage;
namespace r_storage
{

// - stream information is stored in the header
//   - this works because we will always start a new block when recording starts
//   - When we roll to a new block we will have duplicate the existing stream information in the new block
//     - So any video query can always access stream information from the block headers of any block it returns data from.
//   - A cool side benefit of this is that the dumbdex can be recreated if it had to be by reading all the blocks.
//
// [header][index][dep_blocks]
// header:
//   uint32_t n_valid_entries,
//   uint32_t n_entries,
//   int64_t base_time,
//   char[16] video_codec_name,
//   char[2048] video_codec_parameters,
//   char[16] audio_codec_name,
//   char[2048] audio_codec_parameters
// index:
//   {uint32_t time, uint32_t block_offset}[n_entries]
// blocks:
//   [uint8_t stream_id, uint32_t block_size, uint8_t payload[block_size]]

struct r_ind_block_info
{
    uint8_t stream_id;
    int64_t ts;
    uint32_t block_size;
    uint8_t* block;
    std::string video_codec_name;
    std::string video_codec_parameters;
    std::string audio_codec_name;
    std::string audio_codec_parameters;
};

class r_ind_block
{
    friend class ::test_r_storage;

    enum r_ind_block_enum
    {
        INDEX_ENTRY_SIZE=8
    };

public:
    class iterator
    {
    public:
        R_API iterator(
            uint8_t* block_start,
            int64_t base_time,
            const std::string& video_codec_name,
            const std::string& video_codec_parameters,
            const std::string& audio_codec_name,
            const std::string& audio_codec_parameters,
            uint8_t* index_start,
            uint32_t n_valid_entries,
            uint32_t pos
        ) :
            _start(block_start),
            _base_time(base_time),
            _video_codec_name(video_codec_name),
            _video_codec_parameters(video_codec_parameters),
            _audio_codec_name(audio_codec_name),
            _audio_codec_parameters(audio_codec_parameters),
            _index_start(index_start),
            _n_valid_entries(n_valid_entries),
            _pos(pos)
        {
            if(_pos > _n_valid_entries)
                R_THROW(("Invalid iterator position"));
        }

        R_API iterator(const iterator& other) :
            _start(other._start),
            _base_time(other._base_time),
            _video_codec_name(other._video_codec_name),
            _video_codec_parameters(other._video_codec_parameters),
            _audio_codec_name(other._audio_codec_name),
            _audio_codec_parameters(other._audio_codec_parameters),
            _index_start(other._index_start),
            _n_valid_entries(other._n_valid_entries),
            _pos(other._pos)
        {
        }

        R_API r_ind_block_info operator*() const
        {
            uint32_t block_offset = *(uint32_t*)(_index_start + ((_pos*INDEX_ENTRY_SIZE) + sizeof(uint32_t)));
            uint8_t* block = _start + block_offset;

            // [uint8_t stream_id][uint8_t flags][uint32_t block_size][uint8_t block[]]
            r_ind_block_info info;
            info.stream_id = *block;
            info.ts = _base_time + *(uint32_t*)(_index_start + (_pos*INDEX_ENTRY_SIZE));
            info.block_size = *(uint32_t*)(block + 1);
            info.block = block + 5;
            info.video_codec_name = _video_codec_name;
            info.video_codec_parameters = _video_codec_parameters;
            info.audio_codec_name = _audio_codec_name;
            info.audio_codec_parameters = _audio_codec_parameters;

            return info;
        }

        R_API iterator& operator=(const iterator& other)
        {
            _start = other._start;
            _base_time = other._base_time;
            _video_codec_name = other._video_codec_name;
            _video_codec_parameters = other._video_codec_parameters;
            _audio_codec_name = other._audio_codec_name;
            _audio_codec_parameters = other._audio_codec_parameters;
            _index_start = other._index_start;
            _n_valid_entries = other._n_valid_entries;
            _pos = other._pos;
            return *this;
        }

        R_API inline int64_t ts() const
        {
            return _base_time + *(uint32_t*)(_index_start + (_pos*INDEX_ENTRY_SIZE));
        }

        R_API inline uint8_t stream_id() const
        {
            uint32_t block_offset = *(uint32_t*)(_index_start + ((_pos*INDEX_ENTRY_SIZE) + sizeof(uint32_t)));
            uint8_t* block = _start + block_offset;
            return *block;
        }

        R_API bool operator==(const iterator& other) const
        {
            return _pos == other._pos && _index_start == other._index_start && _n_valid_entries == other._n_valid_entries;
        }

        R_API bool next()
        {
            if(_pos < _n_valid_entries)
            {
                ++_pos;
                return true;
            }
            return false;
        }

        R_API bool prev()
        {
            if(_pos > 0)
            {
                --_pos;
                return true;
            }
            return false;
        }

        R_API bool valid() const
        {
            return _pos < _n_valid_entries;
        }

    private:
        uint8_t* _start;
        int64_t _base_time;
        std::string _video_codec_name;
        std::string _video_codec_parameters;
        std::string _audio_codec_name;
        std::string _audio_codec_parameters;
        uint8_t* _index_start;
        uint32_t _n_valid_entries;
        uint32_t _pos;
    };

    r_ind_block() :
        _start(nullptr),
        _size(0)
    {
    }

    r_ind_block(uint8_t* p, size_t size);

    r_ind_block(const r_ind_block&) = delete;

    r_ind_block(r_ind_block&& other) noexcept :
        _start(std::move(other._start)),
        _size(std::move(other._size))
    {
    }

    ~r_ind_block() noexcept;

    r_ind_block& operator=(const r_ind_block&) = delete;

    r_ind_block& operator=(r_ind_block&& other) noexcept
    {
        _start = std::move(other._start);
        other._start = nullptr;
        _size = std::move(other._size);
        other._size = 0;

        return *this;
    }

    bool fits(size_t size) const;

    void append(const uint8_t* data, size_t size, uint8_t stream_id, int64_t ts);

    iterator begin() const
    {
        return iterator(
            _start,
            _read_base_time(),
            _read_video_codec_name(),
            _read_video_codec_parameters(),
            _read_audio_codec_name(),
            _read_audio_codec_parameters(),
            _index_start(),
            _read_n_valid_entries(),
            0
        );
    }

    iterator end() const
    {
        auto n_valid_entries = _read_n_valid_entries();
        return iterator(
            _start,
            _read_base_time(),
            _read_video_codec_name(),
            _read_video_codec_parameters(),
            _read_audio_codec_name(),
            _read_audio_codec_parameters(),
            _index_start(),
            n_valid_entries,
            n_valid_entries
        );
    }

    iterator find_lower_bound(int64_t ts) const
    {
        auto index_start = _index_start();
        auto n_valid_entries = _read_n_valid_entries();
        auto index_end = index_start + (n_valid_entries * INDEX_ENTRY_SIZE);
        int64_t base_time = _read_base_time();

        // If the ts is before the first entry return the first entry...
        if(ts < base_time)
            return begin();

        auto offset_time = (uint32_t)(ts - base_time);

        auto result = r_utils::lower_bound_bytes(
            index_start,
            index_end,
            (uint8_t*)&offset_time,
            INDEX_ENTRY_SIZE,
            [base_time](const uint8_t* a, const uint8_t* b) -> int {
                int64_t ts_a = base_time + *(uint32_t*)a;
                int64_t ts_b = base_time + *(uint32_t*)b;
                return (ts_a < ts_b) ? -1 : ((ts_a == ts_b) ? 0 : 1);
            }
        );

        if(result == index_end)
            return end();

        return iterator(
            _start,
            _read_base_time(),
            _read_video_codec_name(),
            _read_video_codec_parameters(),
            _read_audio_codec_name(),
            _read_audio_codec_parameters(),
            index_start,
            n_valid_entries,
            (uint32_t)(result - index_start)/INDEX_ENTRY_SIZE
        );
    }

    static void initialize_block(
        uint8_t* p,
        size_t size,
        uint32_t n_entries,
        int64_t base_time,
        const std::string& video_codec_name,
        const std::string& video_codec_parameters,
        const std::string& audio_codec_name,
        const std::string& audio_codec_parameters
    );

private:
    uint32_t _read_n_valid_entries() const;
    void _write_n_valid_entries(uint32_t n_valid_entries);

    uint32_t _read_n_entries() const;
    void _write_n_entries(uint32_t n_entries);

    int64_t _read_base_time() const;
    void _write_base_time(int64_t base_time);

    std::string _read_video_codec_name() const;
    void _write_video_codec_name(const std::string& video_codec_name);

    std::string _read_video_codec_parameters() const;
    void _write_video_codec_parameters(const std::string& video_codec_parameters);

    std::string _read_audio_codec_name() const;
    void _write_audio_codec_name(const std::string& audio_codec_name);

    std::string _read_audio_codec_parameters() const;
    void _write_audio_codec_parameters(const std::string& audio_codec_parameters);

    uint8_t* _index_start() const;
    uint8_t* _blocks_start() const;

    uint8_t* _start;
    size_t _size;
};

}

#endif
