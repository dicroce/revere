#ifndef __r_storage_r_ring_h
#define __r_storage_r_ring_h

#include "r_utils/r_file.h"
#include "r_utils/r_file_lock.h"
#include "r_utils/r_memory_map.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_time_utils.h"
#include <memory>
#include <string>
#include <chrono>
#include <vector>
#include <ctime>

namespace r_storage
{

class r_ring final
{
public:
    R_API r_ring(const std::string& path, size_t element_size);
    R_API r_ring(const r_ring&) = delete;
    R_API r_ring(r_ring&& other) noexcept;
    R_API ~r_ring() noexcept;
    
    R_API r_ring& operator=(const r_ring&) = delete;
    R_API r_ring& operator=(r_ring&& other) noexcept;

    R_API void write(const std::chrono::system_clock::time_point& tp, const uint8_t* p);

    template<typename CB>
    void query(const std::chrono::system_clock::time_point& qs, const std::chrono::system_clock::time_point& qe, CB cb)
    {
        r_utils::r_file_lock_guard g(_lock, false);
        auto n_elements = _n_elements();

        auto now = std::chrono::system_clock::now();
        std::time_t now_et = std::chrono::system_clock::to_time_t(now);

        if(qe <= qs)
            R_THROW(("invalid query: " + r_utils::r_time_utils::tp_to_iso_8601(qs, false) + " to " + r_utils::r_time_utils::tp_to_iso_8601(qe, false)));

        std::time_t qs_et = std::chrono::system_clock::to_time_t(qs);
        std::time_t oldest_et = now_et - n_elements;

        if(qs_et < oldest_et)
        {
            printf("qs = %lu, oldest = %lu\n",qs_et,oldest_et);
            fflush(stdout);
            R_THROW(("query start time is too old"));
        }

        if(qe > now)
            R_THROW(("query end time is too new"));

        auto start_idx = _idx(qs);
        auto elements_to_query = std::chrono::duration_cast<std::chrono::seconds>(qe-qs).count();
        for(auto i = 0; i < elements_to_query; i++)
        {
            uint8_t* element = _ring_start() + (((start_idx + i) % n_elements) * _element_size);
            cb(element);
        }
    }

    R_API std::vector<uint8_t> query_raw(const std::chrono::system_clock::time_point& qs, const std::chrono::system_clock::time_point& qe)
    {
        r_utils::r_file_lock_guard g(_lock, false);
        auto n_elements = _n_elements();

        auto now = std::chrono::system_clock::now();
        std::time_t now_et = std::chrono::system_clock::to_time_t(now);

        if(qe <= qs)
            R_THROW(("invalid query"));

        std::time_t qs_et = std::chrono::system_clock::to_time_t(qs);
        std::time_t oldest_et = now_et - n_elements;

        if(qs_et < oldest_et)
            R_THROW(("query start time is too old"));

        if(qe > now)
            R_THROW(("query end time is too new"));

        auto start_idx = _idx(qs);
        auto elements_to_query = std::chrono::duration_cast<std::chrono::seconds>(qe-qs).count();
        std::vector<uint8_t> result(elements_to_query * _element_size);

        auto elements_before_wrap = (std::min)((int64_t)(n_elements - start_idx), elements_to_query);
        auto elements_after_wrap = elements_to_query - elements_before_wrap;

        memcpy(result.data(), _ring_start() + (start_idx * _element_size), elements_before_wrap * _element_size);

        if(elements_after_wrap > 0)
        {
            memcpy(result.data() + (elements_before_wrap * _element_size), _ring_start(), elements_after_wrap * _element_size);
        }

        return result;
    }

    R_API static void allocate(const std::string& path, size_t element_size, size_t n_elements);

private:
    R_API uint8_t* _ring_start() const;
    R_API size_t _n_elements() const;
    std::chrono::system_clock::time_point _created_at() const;
    R_API size_t _idx(const std::chrono::system_clock::time_point& tp) const;
    size_t _unwrapped_idx(const std::chrono::system_clock::time_point& tp) const;

    r_utils::r_file _file;
    r_utils::r_file_lock _lock;
    size_t _element_size;
    size_t _file_size;
    r_utils::r_memory_map _map;
    int64_t _last_write_idx;
};

}

#endif
