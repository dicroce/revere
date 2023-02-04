
#include "r_storage/r_ind_block.h"
#include "r_utils/r_std_utils.h"
#include "r_utils/r_macro.h"
#include <cstring>

using namespace r_storage;
using namespace r_utils;
using namespace std;

r_ind_block::r_ind_block(uint8_t* p, size_t size) :
    _start(p),
    _size(size)
{
}

r_ind_block::~r_ind_block() noexcept
{
}

bool r_ind_block::fits(size_t size) const
{
    if(_start == nullptr || size == 0)
        R_THROW(("Cannot compute fits() on empty index."));

    auto n_valid_entries = _read_n_valid_entries();

    if(n_valid_entries >= _read_n_entries())
        return false;

    uint8_t* last_index = _index_start() + ((n_valid_entries-1)*INDEX_ENTRY_SIZE);
    uint8_t* last_block = _start + *(uint32_t*)(last_index + 4);
    uint8_t* new_block_start = last_block + 5 + *(uint32_t*)(last_block + 1);
    uint8_t* new_block_end = new_block_start + (5 + size);

    auto end_sentry = _start + _size;
    if(new_block_end > end_sentry)
        return false;

    return true;
}

void r_ind_block::append(const uint8_t* data, size_t size, uint8_t stream_id, int64_t ts)
{
    if(_start == nullptr || size == 0)
        R_THROW(("Cannot append to empty index."));

    uint32_t n_entries = _read_n_entries();
    uint32_t n_valid_entries = _read_n_valid_entries();

    if(n_valid_entries >= n_entries)
        R_THROW(("Block is full."));

    if(n_valid_entries == 0)
    {
        _write_base_time(ts);

        // write block...
        uint8_t* block = _blocks_start();
        *block = stream_id;
        *(uint32_t*)(block + 1) = (uint32_t)size;
        memcpy(block + 5, data, size);

        // write index...
        uint8_t* index = _index_start();
        *(uint32_t*)index = 0;
        *(uint32_t*)(index + 4) = (uint32_t)(block - _start);

        FULL_MEM_BARRIER();

        _write_n_valid_entries(1);
    }
    else
    {
        int64_t base_time = _read_base_time();
        int64_t big_offset_time = ts - base_time;
        if(big_offset_time > 86400000)
            R_THROW(("Too much time in block!"));
        if(big_offset_time < 0)
            R_THROW(("Negative time in block! ts=%ld base_time=%ld\n", ts, base_time));

        uint32_t offset_time = static_cast<uint32_t>(big_offset_time);

        uint8_t* last_index = _index_start() + ((n_valid_entries-1)*INDEX_ENTRY_SIZE);

        // an index entry is [uint32_t time, uint32_t offset] so skip the time and read the offset
        uint8_t* last_block = _start + *(uint32_t*)(last_index + 4);

        uint8_t* new_block = last_block + 5 + *(uint32_t*)(last_block + 1);

        // write block...
        *new_block = stream_id;
        *(uint32_t*)(new_block + 1) = (uint32_t)size;
        memcpy(new_block + 5, data, size);

        // write index...
        uint8_t* new_index = _index_start() + (n_valid_entries*INDEX_ENTRY_SIZE);
        *(uint32_t*)new_index = offset_time;
        *(uint32_t*)(new_index + 4) = (uint32_t)(new_block - _start);

        FULL_MEM_BARRIER();

        _write_n_valid_entries(n_valid_entries + 1);
    }
}

void r_ind_block::initialize_block(uint8_t* p, size_t size, uint32_t n_entries, int64_t base_time, const std::string& video_codec_name, const std::string& video_codec_parameters, const std::string& audio_codec_name, const std::string& audio_codec_parameters)
{
    if(size < 8192)
        R_THROW(("Block size too small."));
    
    if(video_codec_name.length() >= 16)
        R_THROW(("Video codec name too long."));

    if(video_codec_parameters.length() >= 2048)
        R_THROW(("Video codec parameters too long."));

    if(audio_codec_name.length() >= 16)
        R_THROW(("Audio codec name too long."));
    
    if(audio_codec_parameters.length() >= 2048)
        R_THROW(("Audio codec parameters too long."));

    // header
    //   uint32_t n_valid_entries,
    //   uint32_t n_entries,
    //   int64_t base_time,
    //   char[16] video_codec_name,
    //   char[2048] video_codec_parameters,
    //   char[16] audio_codec_name,
    //   char[2048] audio_codec_parameters

    *(uint32_t*)p = 0;
    *(uint32_t*)(p+4) = n_entries;
    *(int64_t*)(p+8) = base_time;
    memset(p+16, 0, 16);
    memcpy(p+16, video_codec_name.c_str(), video_codec_name.length());
    memset(p+32, 0, 2048);
    memcpy(p+32, video_codec_parameters.c_str(), video_codec_parameters.length());
    memset(p+2080, 0, 16);
    memcpy(p+2080, audio_codec_name.c_str(), audio_codec_name.length());
    memset(p+2096, 0, 2048);
    memcpy(p+2096, audio_codec_parameters.c_str(), audio_codec_parameters.length());
}

uint32_t r_ind_block::_read_n_valid_entries() const
{
    return *(uint32_t*)_start;
}

void r_ind_block::_write_n_valid_entries(uint32_t n_valid_entries)
{
    *(uint32_t*)_start = n_valid_entries;
}

uint32_t r_ind_block::_read_n_entries() const
{
    return *(uint32_t*)(_start + 4);
}

void r_ind_block::_write_n_entries(uint32_t n_entries)
{
    *(uint32_t*)(_start + 4) = n_entries;
}

int64_t r_ind_block::_read_base_time() const
{
    return *(int64_t*)(_start + 8);
}

void r_ind_block::_write_base_time(int64_t base_time)
{
    *(int64_t*)(_start + 8) = base_time;
}

string r_ind_block::_read_video_codec_name() const
{
    return string((char*)(_start + 16));
}

void r_ind_block::_write_video_codec_name(const string& video_codec_name)
{
    if(video_codec_name.length() >= 16)
        R_THROW(("Video codec name too long"));
    memset(_start + 16, 0, 16);
    memcpy(_start + 16, video_codec_name.c_str(), video_codec_name.length());
}

string r_ind_block::_read_video_codec_parameters() const
{
    return string((char*)(_start + 32));
}

void r_ind_block::_write_video_codec_parameters(const string& video_codec_parameters)
{
    if(video_codec_parameters.length() >= 2048)
        R_THROW(("Video codec parameters too long"));
    memset(_start + 32, 0, 2048);
    memcpy(_start + 32, video_codec_parameters.c_str(), video_codec_parameters.length());
}

string r_ind_block::_read_audio_codec_name() const
{
    return string((char*)(_start + 2080));
}

void r_ind_block::_write_audio_codec_name(const string& audio_codec_name)
{
    if(audio_codec_name.length() >= 16)
        R_THROW(("Audio codec name too long"));
    memset(_start + 2080, 0, 16);
    memcpy(_start + 2080, audio_codec_name.c_str(), audio_codec_name.length());
}

string r_ind_block::_read_audio_codec_parameters() const
{
    return string((char*)(_start + 2096));
}

void r_ind_block::_write_audio_codec_parameters(const string& audio_codec_parameters)
{
    if(audio_codec_parameters.length() >= 2048)
        R_THROW(("Video codec parameters too long"));
    memset(_start + 2096, 0, 2048);
    memcpy(_start + 2096, audio_codec_parameters.c_str(), audio_codec_parameters.length());
}

uint8_t* r_ind_block::_index_start() const
{
    return _start + 4144;
}

uint8_t* r_ind_block::_blocks_start() const
{
    return _index_start() + (_read_n_entries() * INDEX_ENTRY_SIZE);
}
