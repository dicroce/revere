#ifndef __r_storage_file_reader_h
#define __r_storage_file_reader_h

#include "r_storage/r_storage_file.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include "nanots.h"
#include <string>
#include <vector>
#include <memory>
#include <climits>

namespace r_storage
{

class r_storage_file_reader final
{
public:
    R_API r_storage_file_reader(const std::string& file_name);

    R_API r_storage_file_reader(const r_storage_file_reader&) = delete;
    R_API r_storage_file_reader(r_storage_file_reader&& other) noexcept = delete;

    R_API ~r_storage_file_reader() noexcept;

    R_API r_storage_file_reader& operator=(const r_storage_file_reader&) = delete;
    R_API r_storage_file_reader& operator=(r_storage_file_reader&& other) noexcept = delete;

    // query() returns an r_blob_tree populated with the query results (same format as original)
    R_API std::vector<uint8_t> query(r_storage_media_type media_type, int64_t start_ts, int64_t end_ts);

    R_API std::vector<uint8_t> query_key(r_storage_media_type media_type, int64_t ts);

    R_API std::vector<std::pair<int64_t, int64_t>> query_segments(int64_t start_ts = 0, int64_t end_ts = LLONG_MAX);

    R_API std::vector<std::pair<int64_t, int64_t>> query_blocks(int64_t start_ts = 0, int64_t end_ts = LLONG_MAX);

    // key_frame_start_times() returns an array of key frame timestamps
    R_API std::vector<int64_t> key_frame_start_times(r_storage_media_type media_type, int64_t start_ts = 0, int64_t end_ts = LLONG_MAX);

    R_API r_utils::r_nullable<int64_t> last_ts();
    R_API r_utils::r_nullable<int64_t> first_ts();

private:
    // Helper structure to hold frame data during merging
    struct frame_data {
        int64_t ts;
        uint8_t stream_id;
        uint8_t flags;
        std::vector<uint8_t> data;
        std::string codec_name;
        std::string codec_parameters;
    };

    // Helper method to merge video and audio frames in timestamp order
    std::vector<frame_data> _merge_frames(
        const std::vector<frame_data>& video_frames,
        const std::vector<frame_data>& audio_frames
    );

    // Helper method to find the closest previous key frame for a given timestamp
    int64_t _find_closest_key_frame(r_storage_media_type media_type, int64_t ts);

    // Helper method to extract codec information from nanots metadata
    void _extract_codec_info(const std::string& metadata, 
                           std::string& video_codec_name, std::string& video_codec_parameters,
                           std::string& audio_codec_name, std::string& audio_codec_parameters);

    std::string _file_name;
    std::unique_ptr<nanots_reader> _reader;
};

}

#endif