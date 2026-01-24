#ifndef r_storage_r_md_storage_file_h__
#define r_storage_r_md_storage_file_h__

#include "nanots.h"
#include "r_utils/r_macro.h"
#include <string>
#include <memory>
#include <map>

namespace r_storage
{

// Metadata storage file for general-purpose analytics data
// Stores JSON blobs organized by stream tags with timestamps
class r_md_storage_file final
{
public:
    // Opens/creates a nanots file for metadata storage
    R_API r_md_storage_file(const std::string& file_name);

    R_API r_md_storage_file(const r_md_storage_file&) = delete;
    R_API r_md_storage_file(r_md_storage_file&& other) noexcept = delete;

    R_API ~r_md_storage_file() noexcept;

    R_API r_md_storage_file& operator=(const r_md_storage_file&) = delete;
    R_API r_md_storage_file& operator=(r_md_storage_file&& other) noexcept = delete;

    // Write metadata to a specific stream
    // stream_tag: identifies the metadata stream (e.g., "person_metadata", "vehicle_metadata")
    // json_data: JSON-formatted metadata
    // timestamp_ms: milliseconds since epoch
    R_API void write_metadata(const std::string& stream_tag, const std::string& json_data, int64_t timestamp_ms);

    // Pre-allocate storage space
    R_API static void allocate(const std::string& file_name, size_t block_size, size_t num_blocks);

private:
    // Get or create a write context for a stream
    R_API write_context& _get_or_create_context(const std::string& stream_tag);

    std::string _file_name;
    std::unique_ptr<nanots_writer> _writer;
    std::map<std::string, std::unique_ptr<write_context>> _write_contexts;
};

}

#endif