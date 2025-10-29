#include "r_storage/r_md_storage_file.h"
#include "r_utils/r_exception.h"
#include <memory>

using namespace r_utils;
using namespace r_storage;
using namespace std;

r_md_storage_file::r_md_storage_file(const string& file_name) :
    _file_name(file_name)
{
    // Use .mdnts extension for metadata nanots files
    auto base_name = file_name.substr(0, (file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".mdnts";

    _writer = make_unique<nanots_writer>(nanots_file_name, true);
}

r_md_storage_file::~r_md_storage_file() noexcept
{
}

void r_md_storage_file::write_metadata(const string& stream_tag, const string& json_data, int64_t timestamp_ms)
{
    if(stream_tag.empty())
        R_THROW(("Stream tag cannot be empty"));
    
    if(json_data.empty())
        R_THROW(("JSON data cannot be empty"));
    
    auto& ctx = _get_or_create_context(stream_tag);
    
    // Write the JSON data as a blob with the timestamp
    // Note: nanots expects data as uint8_t*, so we cast the string data
    _writer->write(ctx, 
                   reinterpret_cast<const uint8_t*>(json_data.data()), 
                   json_data.size(), 
                   timestamp_ms, 
                   0); // flags = 0 for metadata
}

write_context& r_md_storage_file::_get_or_create_context(const string& stream_tag)
{
    auto it = _write_contexts.find(stream_tag);
    if(it != _write_contexts.end())
        return *it->second;
    
    // Create a new write context for this stream
    // The stream tag is used as the stream name in nanots
    // Empty metadata string since we don't need codec info for JSON
    auto wc = make_unique<write_context>(_writer->create_write_context(stream_tag, ""));
    auto& ref = *wc;
    _write_contexts[stream_tag] = move(wc);
    
    return ref;
}

void r_md_storage_file::allocate(const string& file_name, size_t block_size, size_t num_blocks)
{
    auto base_name = file_name.substr(0, (file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".mdnts";
    
    nanots_writer::allocate(nanots_file_name, static_cast<uint32_t>(block_size), static_cast<uint32_t>(num_blocks));
}