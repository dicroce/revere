
#ifndef __r_storage_file_reader_h
#define __r_storage_file_reader_h

#include "r_storage/r_dumbdex.h"
#include "r_storage/r_storage_file.h"
#include "r_utils/r_memory_map.h"
#include "r_utils/r_file.h"
#include "r_utils/r_file_lock.h"
#include "r_utils/r_macro.h"
#include "r_utils/r_nullable.h"
#include <string>
#include <vector>
#include <memory>
#include <climits>

namespace r_storage
{

class r_storage_file;

class r_storage_file_reader final
{
    friend class r_storage_file;
public:
    R_API r_storage_file_reader(const std::string& file_name);

    R_API r_storage_file_reader(const r_storage_file_reader&) = delete;
    R_API r_storage_file_reader(r_storage_file_reader&& other) noexcept = delete;

    R_API ~r_storage_file_reader() noexcept;

    R_API r_storage_file_reader& operator=(const r_storage_file_reader&) = delete;
    R_API r_storage_file_reader& operator=(r_storage_file_reader&& other) noexcept = delete;

    // query() returns an r_blob_tree populated with the query results (see unit tests).
    R_API std::vector<uint8_t> query(r_storage_media_type media_type, int64_t start_ts, int64_t end_ts);

    R_API std::vector<uint8_t> query_key(r_storage_media_type media_type, int64_t ts);

    R_API std::vector<std::pair<int64_t, int64_t>> query_segments(int64_t start_ts = 0, int64_t end_ts = LLONG_MAX);

    R_API std::vector<std::pair<int64_t, int64_t>> query_blocks(int64_t start_ts = 0, int64_t end_ts = LLONG_MAX);

    // key_frame_start_times() returns an array of the independent block ts's
    R_API std::vector<int64_t> key_frame_start_times(r_storage_media_type media_type, int64_t start_ts = 0, int64_t end_ts = LLONG_MAX);

    R_API r_utils::r_nullable<int64_t> last_ts();
    R_API r_utils::r_nullable<int64_t> first_ts();

private:
    enum r_storage_file_enum
    {
        R_STORAGE_FILE_HEADER_SIZE = 128
    };

    static r_storage_file::_storage_file_header _read_header(const std::string& file_name);

    template<typename CB>
    void _visit_ind_blocks(r_storage_media_type media_type, int64_t start_ts, int64_t end_ts, CB cb);

    std::vector<int64_t> _query_ind_blocks_ts(r_storage_media_type media_type, int64_t start_ts, int64_t end_ts);

    template<typename CB>
    void _visit_ind_block(r_storage_media_type media_type, int64_t ts, CB cb);

    std::unique_ptr<r_utils::r_memory_map> _map_block(uint16_t block);

    R_API r_utils::r_nullable<int64_t> _last_ts();
    R_API r_utils::r_nullable<int64_t> _first_ts();

    std::string _file_name;
    r_utils::r_file _file;
    r_utils::r_file_lock _file_lock;
    r_storage_file::_storage_file_header _h;
    std::shared_ptr<r_utils::r_memory_map> _dumbdex_map;
    r_dumbdex _block_index;    
};

}

#endif

