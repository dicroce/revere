
#ifndef __r_vss_r_motion_storage_sink_h
#define __r_vss_r_motion_storage_sink_h

#include "r_storage/r_ring.h"
#include <chrono>
#include <cstdint>
#include <string>

namespace r_vss
{

class r_motion_storage_sink
{
public:
    virtual ~r_motion_storage_sink() = default;
    virtual void write(const std::chrono::system_clock::time_point& tp, const uint8_t* p) = 0;
    virtual void write_range(const std::chrono::system_clock::time_point& start,
                             const std::chrono::system_clock::time_point& end,
                             const uint8_t* p) = 0;
};

class r_ring_storage_sink final : public r_motion_storage_sink
{
public:
    r_ring_storage_sink(const std::string& path, size_t element_size) :
        _ring(path, element_size)
    {}

    void write(const std::chrono::system_clock::time_point& tp, const uint8_t* p) override
    {
        _ring.write(tp, p);
    }

    void write_range(const std::chrono::system_clock::time_point& start,
                     const std::chrono::system_clock::time_point& end,
                     const uint8_t* p) override
    {
        _ring.write_range(start, end, p);
    }

private:
    r_storage::r_ring _ring;
};

}

#endif
