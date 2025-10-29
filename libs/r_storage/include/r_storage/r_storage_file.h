#ifndef r_storage_r_storage_file_h__
#define r_storage_r_storage_file_h__

#include "r_storage/r_storage_file.h"
#include "nanots.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <string>
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
    std::unique_ptr<write_context> wc;
    std::string codec_name;
    std::string codec_parameters;
};

class r_storage_file final
{
public:
    R_API r_storage_file(const std::string& file_name);

    R_API r_storage_file(const r_storage_file&) = delete;
    R_API r_storage_file(r_storage_file&& other) noexcept = delete;

    R_API ~r_storage_file() noexcept;

    R_API r_storage_file& operator=(const r_storage_file&) = delete;
    R_API r_storage_file& operator=(r_storage_file&& other) noexcept = delete;

    R_API r_storage_write_context create_write_context(
        const std::string& codec_name,
        const r_utils::r_nullable<std::string>& codec_parameters,
        r_storage_media_type media_type
    );

    R_API void write_frame(const r_storage_write_context& ctx, r_storage_media_type media_type, const uint8_t* p, size_t size, bool key, int64_t ts, int64_t pts);

    R_API static size_t remove_blocks(const std::string& file_name, int64_t start_ts, int64_t end_ts);

    R_API static void allocate(const std::string& file_name, size_t block_size, size_t num_blocks);

private:
    std::string _file_name;
    std::unique_ptr<nanots_writer> _writer;
    int64_t _last_video_ts {-1};
    int64_t _video_ts_correction {0};
    int64_t _last_audio_ts {-1};
    int64_t _audio_ts_correction {0};
};

R_API std::pair<int64_t, int64_t> required_file_size_for_retention_hours(int64_t retention_hours, int64_t byte_rate);
R_API std::string human_readable_file_size(double size);

}

#endif