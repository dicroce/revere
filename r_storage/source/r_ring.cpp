
#include "r_storage/r_ring.h"
#include "r_utils/r_exception.h"

using namespace std;
using namespace std::chrono;
using namespace r_utils;
using namespace r_storage;

static const uint8_t R_RING_HEADER_SIZE = 4;

// [header]
// [ring buffer]
//
// [header
//     uint32_t created_at
// ]
//
// [ring buffer
//    [element]
//    [element]
//    ...
// ]
//
// [element
//     uint8_t motion (0-100)
//     uint8_t avg_motion (0-100)
//     uint8_t stddev (0-100)
// ]

r_ring::r_ring(const string& path, size_t element_size) :
    _file(r_file::open(path, "r+")),
    _lock(r_fs::fileno(_file)),
    _element_size(element_size),
    _file_size(r_fs::file_size(path)),
    _map(r_fs::fileno(_file), 0, (uint32_t)_file_size, r_memory_map::RMM_PROT_READ | r_memory_map::RMM_PROT_WRITE, r_memory_map::RMM_TYPE_FILE | r_memory_map::RMM_SHARED),
    _last_write_idx(-1)
{
}

r_ring::r_ring(r_ring&& other) noexcept :
    _file(move(other._file)),
    _lock(move(other._lock)),
    _element_size(other._element_size),
    _file_size(other._file_size),
    _map(move(other._map)),
    _last_write_idx(other._last_write_idx)
{
}

r_ring::~r_ring() noexcept
{
}

r_ring& r_ring::operator=(r_ring&& other) noexcept
{
    _last_write_idx = other._last_write_idx;
    _map = move(other._map);
    _file_size = other._file_size;
    _element_size = other._element_size;
    _lock = move(other._lock);
    _file = move(other._file);

    return *this;
}

void r_ring::write(const system_clock::time_point& tp, const uint8_t* p)
{
    r_file_lock_guard g(_lock);

    auto n_elements = _n_elements();

    auto unwrapped_idx = _unwrapped_idx(tp);

    if(_last_write_idx != -1)
    {
        auto delta = abs((int)(unwrapped_idx - _last_write_idx));

        if(delta > 1)
        {
            for(size_t i = 0; i < delta-1; i++)
            {
                auto ofs = ((_last_write_idx + 1 + i) % n_elements) * _element_size;
                memset(_ring_start() + ofs, 0, _element_size);
            }
        }
    }

    _last_write_idx = unwrapped_idx;

    memcpy(_ring_start() + ((unwrapped_idx % n_elements) * _element_size), p, _element_size);
}

uint8_t* r_ring::_ring_start() const
{
    return (uint8_t*)_map.map() + R_RING_HEADER_SIZE;
}

size_t r_ring::_n_elements() const
{
    return (_file_size - R_RING_HEADER_SIZE) / _element_size;
}

system_clock::time_point r_ring::_created_at() const
{
    uint32_t created_at_ts = *(uint32_t*)_map.map();
    return system_clock::from_time_t(created_at_ts);
}

size_t r_ring::_idx(const system_clock::time_point& tp) const
{
    return _unwrapped_idx(tp) % _n_elements();
}

size_t r_ring::_unwrapped_idx(const std::chrono::system_clock::time_point& tp) const
{
    return duration_cast<seconds>((tp - _created_at())).count();
}

void r_ring::allocate(const string& path, size_t element_size, size_t n_elements)
{
    size_t size = R_RING_HEADER_SIZE + (element_size * n_elements);

    {
#ifdef IS_WINDOWS
        FILE* fp = nullptr;
        fopen_s(&fp, path.c_str(), "w+");
#endif
        std::unique_ptr<FILE, decltype(&fclose)> f(
#ifdef IS_WINDOWS
            fp,
#endif
#ifdef IS_LINUX
            fopen(path.c_str(), "w+"),
#endif
            &fclose
        );

        if(!f)
            R_THROW(("unable to create r_ring file."));

        if(r_fs::fallocate(f.get(), size) < 0)
            R_THROW(("unable to allocate file."));
    }

    {
#ifdef IS_WINDOWS
        FILE* fp = nullptr;
        fopen_s(&fp, path.c_str(), "r+");
#endif
        std::unique_ptr<FILE, decltype(&fclose)> f(
#ifdef IS_WINDOWS
            fp,
#endif
#ifdef IS_LINUX
            fopen(path.c_str(), "r+"),
#endif
            &fclose
        );

        if(!f)
            throw std::runtime_error("Unable to open r_ring file.");

        r_memory_map mm(
            r_fs::fileno(f.get()),
            0,
            (uint32_t)size,
            r_memory_map::RMM_PROT_READ | r_memory_map::RMM_PROT_WRITE,
            r_memory_map::RMM_TYPE_FILE | r_memory_map::RMM_SHARED
        );

        uint8_t* p = (uint8_t*)mm.map();

        memset(p, 0, size);

        uint32_t now = (uint32_t)chrono::duration_cast<chrono::seconds>(chrono::system_clock::now().time_since_epoch()).count();

        *(uint32_t*)p = now;
    }
}