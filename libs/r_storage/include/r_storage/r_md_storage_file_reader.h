#ifndef r_storage_r_md_storage_file_reader_h__
#define r_storage_r_md_storage_file_reader_h__

#include "nanots.h"
#include "r_utils/r_macro.h"
#include <string>
#include <vector>
#include <memory>
#include <climits>

namespace r_storage
{

// Structure to hold metadata query results
struct r_metadata_entry
{
    std::string stream_tag;     // The stream tag (e.g., "person_metadata")
    std::string json_data;      // The JSON metadata
    int64_t timestamp_ms;       // Timestamp in milliseconds since epoch
};

// Reader for metadata storage files
class r_md_storage_file_reader final
{
public:
    // Opens an existing .mdnts file for reading
    R_API r_md_storage_file_reader(const std::string& file_name);

    R_API r_md_storage_file_reader(const r_md_storage_file_reader&) = delete;
    R_API r_md_storage_file_reader(r_md_storage_file_reader&& other) noexcept = delete;

    R_API ~r_md_storage_file_reader() noexcept;

    R_API r_md_storage_file_reader& operator=(const r_md_storage_file_reader&) = delete;
    R_API r_md_storage_file_reader& operator=(r_md_storage_file_reader&& other) noexcept = delete;

    // Query metadata from a specific stream within a time range
    // stream_tag: the stream to query (e.g., "person_metadata")
    // start_ts, end_ts: time range in milliseconds since epoch
    R_API std::vector<r_metadata_entry> query_stream(const std::string& stream_tag, int64_t start_ts, int64_t end_ts = LLONG_MAX);

    // Query metadata from all streams within a time range
    R_API std::vector<r_metadata_entry> query_all(int64_t start_ts, int64_t end_ts = LLONG_MAX);

    // Get list of all stream tags available in the file
    R_API std::vector<std::string> get_stream_tags();

    // Get the time range of data available in the file
    R_API std::pair<int64_t, int64_t> get_time_range();

    // Get the time range of data available for a specific stream
    R_API std::pair<int64_t, int64_t> get_stream_time_range(const std::string& stream_tag);

private:
    std::string _file_name;
    std::unique_ptr<nanots_reader> _reader;
};

}

#endif