
#ifndef r_storage_r_storage_file_h__
#define r_storage_r_storage_file_h__

#include "r_storage/r_dumbdex.h"
#include "r_storage/r_ind_block.h"
#include "r_utils/r_memory_map.h"
#include "r_utils/r_file.h"
#include "r_utils/r_file_lock.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <string>
#include <vector>
#include <deque>
#include <memory>

namespace r_storage
{

enum r_storage_media_type
{
    R_STORAGE_MEDIA_TYPE_VIDEO,
    R_STORAGE_MEDIA_TYPE_AUDIO,
    R_STORAGE_MEDIA_TYPE_ALL,

    R_STORAGE_MEDIA_TYPE_MAX
};

struct r_storage_write_context
{
    std::string video_codec_name;
    std::string video_codec_parameters;
    std::string audio_codec_name;
    std::string audio_codec_parameters;
};

class r_storage_file_reader;

class r_storage_file final
{
    friend class r_storage_file_reader;
public:
    R_API r_storage_file(const std::string& file_name);

    R_API r_storage_file(const r_storage_file&) = delete;
    R_API r_storage_file(r_storage_file&& other) noexcept = delete;

    R_API ~r_storage_file() noexcept;

    R_API r_storage_file& operator=(const r_storage_file&) = delete;
    R_API r_storage_file& operator=(r_storage_file&& other) noexcept = delete;

    R_API r_storage_write_context create_write_context(
        const std::string& video_codec_name,
        const r_utils::r_nullable<std::string>& video_codec_parameters,
        const r_utils::r_nullable<std::string>& audio_codec_name,
        const r_utils::r_nullable<std::string>& audio_codec_parameters
    );

    R_API void write_frame(const r_storage_write_context& ctx, r_storage_media_type media_type, const uint8_t* p, size_t size, bool key, int64_t ts, int64_t pts);

    R_API void finalize(const r_storage_write_context& ctx);

    R_API static void allocate(const std::string& file_name, size_t block_size, size_t num_blocks);

    R_API static std::pair<int64_t, int64_t> required_file_size_for_retention_hours(int64_t retention_hours, int64_t byte_rate);

    R_API static std::string human_readable_file_size(double size);

private:

    struct _storage_file_header
    {
        uint32_t num_blocks;
        uint32_t block_size;
    };

    struct _gop
    {
        bool complete {false};
        int64_t ts {0};
        std::vector<uint8_t> data;
        r_storage_media_type media_type;
    };

    enum r_storage_file_enum
    {
        R_STORAGE_FILE_HEADER_SIZE = 128
    };

    static _storage_file_header _read_header(const std::string& file_name);

    std::shared_ptr<r_utils::r_memory_map> _map_block(uint16_t block);
    std::shared_ptr<r_ind_block> _initialize_ind_block(const r_storage_write_context& ctx, uint16_t block, int64_t ts, size_t n_indexes);

    bool _buffer_full() const;

    void _delete_segments_older_than(int64_t ts);

    r_utils::r_file _file;
    std::string _file_name;
    r_utils::r_file_lock _file_lock;
    _storage_file_header _h;
    std::shared_ptr<r_utils::r_memory_map> _dumbdex_map;
    r_dumbdex _block_index;
    std::deque<_gop> _gop_buffer;
    
    std::shared_ptr<r_utils::r_memory_map> _ind_map;
    std::shared_ptr<r_ind_block> _current_block;

    r_utils::r_nullable<int64_t> _first_ts;
    r_utils::r_nullable<int64_t> _last_ts;
};

}

#endif
