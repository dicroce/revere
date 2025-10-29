#include "r_storage/r_md_storage_file_reader.h"
#include "r_utils/r_exception.h"
#include <memory>
#include <algorithm>

using namespace r_utils;
using namespace r_storage;
using namespace std;

r_md_storage_file_reader::r_md_storage_file_reader(const string& file_name) :
    _file_name(file_name)
{
    // Use .mdnts extension for metadata nanots files
    auto base_name = file_name.substr(0, (file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".mdnts";
    
    _reader = make_unique<nanots_reader>(nanots_file_name);
}

r_md_storage_file_reader::~r_md_storage_file_reader() noexcept
{
}

vector<r_metadata_entry> r_md_storage_file_reader::query_stream(const string& stream_tag, int64_t start_ts, int64_t end_ts)
{
    if(stream_tag.empty())
        R_THROW(("Stream tag cannot be empty"));
    
    vector<r_metadata_entry> results;
    
    try {
        // Use the nanots reader callback interfac
        _reader->read(stream_tag, start_ts, end_ts, 
            [&](const uint8_t* data, size_t size, uint8_t flags, int64_t timestamp, int64_t block_sequence, const string& metadata) {
                r_metadata_entry entry;
                entry.stream_tag = stream_tag;
                entry.timestamp_ms = timestamp;
                
                // Convert the data back to string
                entry.json_data = string(reinterpret_cast<const char*>(data), size);
                
                results.push_back(move(entry));
            });
    } catch(const nanots_exception&) {
        // Stream might not exist or have data in this range
        // Return empty results rather than throwing
    }
    
    return results;
}

vector<r_metadata_entry> r_md_storage_file_reader::query_all(int64_t start_ts, int64_t end_ts)
{
    vector<r_metadata_entry> results;
    
    // Get all stream tags and query each one
    auto stream_tags = get_stream_tags();
    for(const auto& stream_tag : stream_tags) {
        auto stream_results = query_stream(stream_tag, start_ts, end_ts);
        results.insert(results.end(), stream_results.begin(), stream_results.end());
    }
    
    // Sort results by timestamp
    sort(results.begin(), results.end(), 
         [](const r_metadata_entry& a, const r_metadata_entry& b) {
             return a.timestamp_ms < b.timestamp_ms;
         });
    
    return results;
}

vector<string> r_md_storage_file_reader::get_stream_tags()
{
    try {
        // Query stream tags for the entire time range
        return _reader->query_stream_tags(0, LLONG_MAX);
    } catch(const nanots_exception&) {
        // Return empty vector if no streams exist
        return vector<string>();
    }
}

pair<int64_t, int64_t> r_md_storage_file_reader::get_time_range()
{
    try {
        auto stream_tags = get_stream_tags();
        if(stream_tags.empty())
            R_THROW(("No streams available in file"));
        
        int64_t earliest = LLONG_MAX;
        int64_t latest = LLONG_MIN;
        
        for(const auto& stream_tag : stream_tags) {
            auto range = get_stream_time_range(stream_tag);
            earliest = min(earliest, range.first);
            latest = max(latest, range.second);
        }
        
        return make_pair(earliest, latest);
    } catch(const nanots_exception& e) {
        R_THROW(("Failed to get time range: %s", e.what()));
    }
}

pair<int64_t, int64_t> r_md_storage_file_reader::get_stream_time_range(const string& stream_tag)
{
    try {
        // Get contiguous segments for the stream to determine time range
        auto segments = _reader->query_contiguous_segments(stream_tag, 0, LLONG_MAX);
        
        if(segments.empty())
            R_THROW(("No data found for stream '%s'", stream_tag.c_str()));
        
        int64_t earliest = LLONG_MAX;
        int64_t latest = LLONG_MIN;
        
        for(const auto& segment : segments) {
            earliest = min(earliest, segment.start_timestamp);
            latest = max(latest, segment.end_timestamp);
        }
        
        return make_pair(earliest, latest);
    } catch(const nanots_exception& e) {
        R_THROW(("Failed to get time range for stream '%s': %s", stream_tag.c_str(), e.what()));
    }
}