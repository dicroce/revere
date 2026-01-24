#include "r_storage/r_storage_file.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_logger.h"
#include "r_utils/3rdparty/json/json.h"
#include <memory>

using namespace r_utils;
using namespace r_storage;
using namespace std;

r_storage_file::r_storage_file(const string& file_name) :
    _file_name(file_name)
{
    auto base_name = file_name.substr(0, (file_name.find_last_of('.')));
    
    // Use .nts extension for nanots files
    auto nanots_file_name = base_name + ".nts";

    _writer = make_unique<nanots_writer>(nanots_file_name, true);
}

r_storage_file::~r_storage_file() noexcept
{
}

r_storage_write_context r_storage_file::create_write_context(
    const string& codec_name,
    const r_nullable<string>& codec_parameters,
    r_storage_media_type media_type
)
{
    r_storage_write_context ctx;

    // Create JSON metadata containing codec information
    nlohmann::json metadata_json;
    metadata_json[(media_type == R_STORAGE_MEDIA_TYPE_VIDEO)?"video_codec_name":"audio_codec_name"] = codec_name;
    metadata_json[(media_type == R_STORAGE_MEDIA_TYPE_VIDEO)?"video_codec_parameters":"audio_codec_parameters"] = (!codec_parameters.is_null()) ? codec_parameters.value() : "";

    string metadata_str = metadata_json.dump();

    // Create separate write contexts for video and audio streams
    if(media_type == R_STORAGE_MEDIA_TYPE_VIDEO)
        ctx.wc = make_unique<write_context>(_writer->create_write_context("video", metadata_str));
    else
        ctx.wc = make_unique<write_context>(_writer->create_write_context("audio", metadata_str));

    return ctx;
}

void r_storage_file::write_frame(const r_storage_write_context& ctx, r_storage_media_type media_type, const uint8_t* p, size_t size, bool key, int64_t ts, int64_t pts)
{
    if(media_type >= R_STORAGE_MEDIA_TYPE_ALL)
        R_THROW(("Invalid storage media type."));

    if(media_type == R_STORAGE_MEDIA_TYPE_VIDEO) {
        if(_last_video_ts != -1) {
            if(ts <= _last_video_ts) {
                _video_ts_correction += (_last_video_ts - ts) + 1;
            }
        }

        _last_video_ts = ts;
        ts += _video_ts_correction;
    }
    else {
        if(_last_audio_ts != -1) {
            if(ts <= _last_audio_ts) {
                _audio_ts_correction += (_last_audio_ts - ts) + 1;
            }
        }

        _last_audio_ts = ts;
        ts += _audio_ts_correction;
    }
    
    uint8_t flags = key ? 1 : 0;

    _writer->write(*ctx.wc, p, size, ts, flags);
}

size_t r_storage_file::remove_blocks(const std::string& file_name, int64_t start_ts, int64_t end_ts)
{
    size_t removed_count = 0;
    
    try {
        nanots_writer::free_blocks(file_name, "video", start_ts, end_ts);
        removed_count++;
    } catch(const nanots_exception&) {
        // Video stream might not exist or have blocks in this range
    }
    
    try {
        nanots_writer::free_blocks(file_name, "audio", start_ts, end_ts);
        removed_count++;
    } catch(const nanots_exception&) {
        // Audio stream might not exist or have blocks in this range
    }
    
    return removed_count;
}

void r_storage_file::allocate(const std::string& file_name, size_t block_size, size_t num_blocks)
{
    auto base_name = file_name.substr(0, (file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".nts";
    
    nanots_writer::allocate(nanots_file_name, static_cast<uint32_t>(block_size), static_cast<uint32_t>(num_blocks));
}

// Free functions (moved from r_storage_file static methods)  
pair<int64_t, int64_t> r_storage::required_file_size_for_retention_hours(int64_t retention_hours, int64_t byte_rate)
{
    // Note: windows mmap implementation requires mapped regions to be a multiple of 65536.
    const int64_t FIFTY_MB_FILE = 52428800;
    const int64_t FUDGE_FACTOR = 2;

    int64_t natural_byte_size = (byte_rate * 60 * 60 * retention_hours);

    int64_t num_blocks = (natural_byte_size / FIFTY_MB_FILE) + FUDGE_FACTOR;

    return make_pair(num_blocks, FIFTY_MB_FILE);
}

string r_storage::human_readable_file_size(double size)
{
    int i = 0;
    const char* units[] = {"bytes", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    while (size > 1024) {
        size /= 1024;
        i++;
    }

    if(i > 8)
        R_THROW(("Size is too big!"));

    return r_string_utils::format("%.2f %s", size, units[i]);
}
