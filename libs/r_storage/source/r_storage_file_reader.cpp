#include "r_storage/r_storage_file_reader.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/3rdparty/json/json.h"
#include <algorithm>
#include <memory>
#include <cstring>
#include <chrono>

using namespace r_utils;
using namespace r_storage;
using namespace std;
using namespace std::chrono;

r_storage_file_reader::r_storage_file_reader(const string& file_name) :
    _file_name(file_name)
{
    auto base_name = file_name.substr(0, (file_name.find_last_of('.')));
    
    // Use .nts extension for nanots files
    auto nanots_file_name = base_name + ".nts";
    
    _reader = make_unique<nanots_reader>(nanots_file_name);
}

r_storage_file_reader::~r_storage_file_reader() noexcept
{
}

vector<uint8_t> r_storage_file_reader::query(r_storage_media_type media_type, int64_t start_ts, int64_t end_ts)
{
    vector<frame_data> video_frames;
    vector<frame_data> audio_frames;
    
    string video_codec_name, video_codec_parameters;
    string audio_codec_name, audio_codec_parameters;
    bool has_video_metadata = false;
    bool has_audio_metadata = false;
    
    auto base_name = _file_name.substr(0, (_file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".nts";
    
    try {
        // Query video frames if needed
        if (media_type == R_STORAGE_MEDIA_TYPE_VIDEO || media_type == R_STORAGE_MEDIA_TYPE_ALL) {
            nanots_iterator video_iterator(nanots_file_name, "video");
            video_iterator.find(start_ts);
            
            // Back up to find previous key frame
            while (video_iterator.valid() && video_iterator->flags == 0) {
                --video_iterator;
                if (!video_iterator.valid()) break;
            }
            
            while (video_iterator.valid() && video_iterator->timestamp < end_ts) {
                frame_data frame;
                frame.ts = video_iterator->timestamp;
                frame.stream_id = R_STORAGE_MEDIA_TYPE_VIDEO;
                frame.flags = video_iterator->flags;
                
                // Get frame data
                auto data_size = video_iterator->size;
                frame.data.resize(data_size);
                memcpy(frame.data.data(), video_iterator->data, data_size);
                
                video_frames.push_back(frame);
                
                // Extract codec info from metadata on first frame
                if (!has_video_metadata) {
                    auto metadata = video_iterator.current_metadata();
                    if (!metadata.empty()) {
                        _extract_codec_info(metadata, video_codec_name, video_codec_parameters,
                                          audio_codec_name, audio_codec_parameters);
                        has_video_metadata = true;
                    }
                }
                
                ++video_iterator;
            }
        }
        
        // Query audio frames if needed
        if (media_type == R_STORAGE_MEDIA_TYPE_AUDIO || media_type == R_STORAGE_MEDIA_TYPE_ALL) {
            nanots_iterator audio_iterator(nanots_file_name, "audio");
            audio_iterator.find(start_ts);
            
            // Back up to find previous key frame
            while (audio_iterator.valid() && audio_iterator->flags == 0) {
                --audio_iterator;
                if (!audio_iterator.valid()) break;
            }
            
            while (audio_iterator.valid() && audio_iterator->timestamp < end_ts) {
                frame_data frame;
                frame.ts = audio_iterator->timestamp;
                frame.stream_id = R_STORAGE_MEDIA_TYPE_AUDIO;
                frame.flags = audio_iterator->flags;
                
                // Get frame data
                auto data_size = audio_iterator->size;
                frame.data.resize(data_size);
                memcpy(frame.data.data(), audio_iterator->data, data_size);
                
                audio_frames.push_back(frame);
                
                // Extract codec info from metadata on first frame
                if (!has_audio_metadata) {
                    auto metadata = audio_iterator.current_metadata();
                    if (!metadata.empty()) {
                        _extract_codec_info(metadata, video_codec_name, video_codec_parameters,
                                          audio_codec_name, audio_codec_parameters);
                        has_audio_metadata = true;
                    }
                }
                
                ++audio_iterator;
            }
        }
    } catch (const nanots_exception&) {
        // Handle case where stream doesn't exist
        // Continue with empty results for that stream
    }
    
    // Merge frames in timestamp order
    auto merged_frames = _merge_frames(video_frames, audio_frames);
    
    // Build blob tree result (same format as original)
    r_blob_tree bt;
    
    size_t fi = 0;
    for (const auto& frame : merged_frames) {
        bt["frames"][fi]["ind_block_ts"] = r_string_utils::int64_to_s(frame.ts);
        bt["frames"][fi]["data"] = frame.data;
        bt["frames"][fi]["ts"] = r_string_utils::int64_to_s(frame.ts);
        bt["frames"][fi]["key"] = (frame.flags > 0) ? string("true") : string("false");
        bt["frames"][fi]["stream_id"] = r_string_utils::uint8_to_s(frame.stream_id);
        ++fi;
    }
    
    bt["video_codec_name"] = video_codec_name;
    bt["video_codec_parameters"] = video_codec_parameters;
    bt["audio_codec_name"] = audio_codec_name;
    bt["audio_codec_parameters"] = audio_codec_parameters;
    bt["has_audio"] = (!audio_frames.empty()) ? string("true") : string("false");
    
    return r_blob_tree::serialize(bt, 1);
}

vector<uint8_t> r_storage_file_reader::query_key(r_storage_media_type media_type, int64_t ts)
{
    if (media_type == R_STORAGE_MEDIA_TYPE_ALL)
        R_THROW(("You cannot query a key frame from media type: ALL!"));
    
    r_blob_tree bt;
    
    string video_codec_name, video_codec_parameters;
    string audio_codec_name, audio_codec_parameters;
    
    auto base_name = _file_name.substr(0, (_file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".nts";
    vector<uint8_t> frame_data;
    try {
        string stream_tag = (media_type == R_STORAGE_MEDIA_TYPE_VIDEO) ? "video" : "audio";
        nanots_iterator iterator(nanots_file_name, stream_tag);
        iterator.find(ts);
        
        // Back up to find the closest previous key frame
        while (iterator.valid() && iterator->flags == 0) {
            --iterator;
            if (!iterator.valid()) break;
        }
        
        if (iterator.valid() && iterator->flags > 0) {
            // Get frame data
            frame_data.resize(iterator->size);
            memcpy(frame_data.data(), iterator->data, iterator->size);

            bt["frames"][0]["ts"] = r_string_utils::int64_to_s(iterator->timestamp);
            bt["frames"][0]["data"] = frame_data;
            
            // Extract codec info from metadata
            auto metadata = iterator.current_metadata();
            if (!metadata.empty()) {
                _extract_codec_info(metadata, video_codec_name, video_codec_parameters,
                                  audio_codec_name, audio_codec_parameters);
            }
        }
    } catch (const nanots_exception&) {
        // Handle case where stream doesn't exist or no key frame found
    }

    if(!video_codec_name.empty())
        bt["video_codec_name"] = video_codec_name;
    if(!video_codec_parameters.empty())
        bt["video_codec_parameters"] = video_codec_parameters;
    return r_blob_tree::serialize(bt, 1);
}

vector<pair<int64_t, int64_t>> r_storage_file_reader::query_segments(int64_t start_ts, int64_t end_ts)
{
    vector<pair<int64_t, int64_t>> segments;
    
    try {
        // Use nanots_reader to query contiguous segments
        auto video_segments = _reader->query_contiguous_segments("video", start_ts, end_ts);
        for (const auto& seg : video_segments) {
            segments.push_back(make_pair(seg.start_timestamp, (seg.end_timestamp==0)?duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count():seg.end_timestamp));
        }
    } catch (const nanots_exception&) {
        try {
            // Fallback to audio stream if video doesn't exist
            auto audio_segments = _reader->query_contiguous_segments("audio", start_ts, end_ts);
            for (const auto& seg : audio_segments) {
                segments.push_back(make_pair(seg.start_timestamp, seg.end_timestamp));
            }
        } catch (const nanots_exception&) {
            // No streams exist, return empty
        }
    }
    
    return segments;
}

vector<pair<int64_t, int64_t>> r_storage_file_reader::query_blocks(int64_t start_ts, int64_t end_ts)
{
    vector<pair<int64_t, int64_t>> blocks;
    
    auto base_name = _file_name.substr(0, (_file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".nts";
    
    try {
        // Use video stream to determine blocks (assuming video is primary)
        nanots_iterator video_iterator(nanots_file_name, "video");
        video_iterator.find(start_ts);
        
        int64_t current_block_start = -1;
        int64_t last_ts = -1;
        
        while (video_iterator.valid() && video_iterator->timestamp < end_ts) {
            int64_t ts = video_iterator->timestamp;
            
            if (current_block_start == -1) {
                current_block_start = ts;
            } else if (ts - last_ts > 1000000) { // Gap larger than 1 second indicates block boundary
                blocks.push_back(make_pair(current_block_start, last_ts));
                current_block_start = ts;
            }
            
            last_ts = ts;
            ++video_iterator;
        }
        
        if (current_block_start != -1 && last_ts != -1) {
            blocks.push_back(make_pair(current_block_start, last_ts));
        }
    } catch (const nanots_exception&) {
        try {
            // Fallback to audio stream if video doesn't exist
            nanots_iterator audio_iterator(nanots_file_name, "audio");
            audio_iterator.find(start_ts);
            
            int64_t current_block_start = -1;
            int64_t last_ts = -1;
            
            while (audio_iterator.valid() && audio_iterator->timestamp < end_ts) {
                int64_t ts = audio_iterator->timestamp;
                
                if (current_block_start == -1) {
                    current_block_start = ts;
                } else if (ts - last_ts > 1000000) { // Gap larger than 1 second indicates block boundary
                    blocks.push_back(make_pair(current_block_start, last_ts));
                    current_block_start = ts;
                }
                
                last_ts = ts;
                ++audio_iterator;
            }
            
            if (current_block_start != -1 && last_ts != -1) {
                blocks.push_back(make_pair(current_block_start, last_ts));
            }
        } catch (const nanots_exception&) {
            // No streams exist, return empty
        }
    }
    
    return blocks;
}

vector<int64_t> r_storage_file_reader::key_frame_start_times(r_storage_media_type media_type, int64_t start_ts, int64_t end_ts)
{
    vector<int64_t> key_frame_times;
    
    if (media_type >= R_STORAGE_MEDIA_TYPE_MAX)
        R_THROW(("Invalid storage media type."));
    
    auto base_name = _file_name.substr(0, (_file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".nts";
    
    try {
        string stream_tag = (media_type == R_STORAGE_MEDIA_TYPE_VIDEO) ? "video" : "audio";
        nanots_iterator iterator(nanots_file_name, stream_tag);
        iterator.find(start_ts);
        
        while (iterator.valid() && iterator->timestamp < end_ts) {
            // Check if this is a key frame (flags > 0)
            if (iterator->flags > 0) {
                key_frame_times.push_back(iterator->timestamp);
            }
            ++iterator;
        }
    } catch (const nanots_exception&) {
        // Handle case where stream doesn't exist
    }
    
    return key_frame_times;
}

r_nullable<int64_t> r_storage_file_reader::last_ts()
{
    r_nullable<int64_t> result;
    
    auto base_name = _file_name.substr(0, (_file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".nts";
    
    try {
        // Try video stream first
        nanots_iterator video_iterator(nanots_file_name, "video");
        
        // Iterate to find the last frame
        int64_t last_video_ts = -1;
        while (video_iterator.valid()) {
            last_video_ts = video_iterator->timestamp;
            ++video_iterator;
        }
        
        if (last_video_ts != -1) {
            result.set_value(last_video_ts);
        }
        
        // Check if audio has a later timestamp
        try {
            nanots_iterator audio_iterator(nanots_file_name, "audio");
            int64_t last_audio_ts = -1;
            while (audio_iterator.valid()) {
                last_audio_ts = audio_iterator->timestamp;
                ++audio_iterator;
            }
            
            if (last_audio_ts != -1) {
                if (result.is_null() || last_audio_ts > result.value()) {
                    result.set_value(last_audio_ts);
                }
            }
        } catch (const nanots_exception&) {
            // Audio stream doesn't exist, use video result
        }
    } catch (const nanots_exception&) {
        try {
            // Video stream doesn't exist, try audio only
            nanots_iterator audio_iterator(nanots_file_name, "audio");
            int64_t last_audio_ts = -1;
            while (audio_iterator.valid()) {
                last_audio_ts = audio_iterator->timestamp;
                ++audio_iterator;
            }
            
            if (last_audio_ts != -1) {
                result.set_value(last_audio_ts);
            }
        } catch (const nanots_exception&) {
            // No streams exist
        }
    }
    
    return result;
}

r_nullable<int64_t> r_storage_file_reader::first_ts()
{
    r_nullable<int64_t> result;
    
    auto base_name = _file_name.substr(0, (_file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".nts";
    
    try {
        // Try video stream first
        nanots_iterator video_iterator(nanots_file_name, "video");
        if (video_iterator.valid()) {
            result.set_value(video_iterator->timestamp);
        }
        
        // Check if audio has an earlier timestamp
        try {
            nanots_iterator audio_iterator(nanots_file_name, "audio");
            if (audio_iterator.valid()) {
                if (result.is_null() || audio_iterator->timestamp < result.value()) {
                    result.set_value(audio_iterator->timestamp);
                }
            }
        } catch (const nanots_exception&) {
            // Audio stream doesn't exist, use video result
        }
    } catch (const nanots_exception&) {
        try {
            // Video stream doesn't exist, try audio only
            nanots_iterator audio_iterator(nanots_file_name, "audio");
            if (audio_iterator.valid()) {
                result.set_value(audio_iterator->timestamp);
            }
        } catch (const nanots_exception&) {
            // No streams exist
        }
    }
    
    return result;
}

vector<r_storage_file_reader::frame_data> r_storage_file_reader::_merge_frames(
    const vector<frame_data>& video_frames,
    const vector<frame_data>& audio_frames)
{
    vector<frame_data> merged;
    merged.reserve(video_frames.size() + audio_frames.size());
    
    auto video_it = video_frames.begin();
    auto audio_it = audio_frames.begin();
    
    // Merge in timestamp order
    while (video_it != video_frames.end() && audio_it != audio_frames.end()) {
        if (video_it->ts <= audio_it->ts) {
            merged.push_back(*video_it);
            ++video_it;
        } else {
            merged.push_back(*audio_it);
            ++audio_it;
        }
    }
    
    // Add remaining frames
    while (video_it != video_frames.end()) {
        merged.push_back(*video_it);
        ++video_it;
    }
    
    while (audio_it != audio_frames.end()) {
        merged.push_back(*audio_it);
        ++audio_it;
    }
    
    return merged;
}

int64_t r_storage_file_reader::_find_closest_key_frame(r_storage_media_type media_type, int64_t ts)
{
    string stream_tag = (media_type == R_STORAGE_MEDIA_TYPE_VIDEO) ? "video" : "audio";
    
    auto base_name = _file_name.substr(0, (_file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".nts";
    
    try {
        nanots_iterator iterator(nanots_file_name, stream_tag);
        iterator.find(ts);
        
        // Back up to find the closest previous key frame
        while (iterator.valid() && iterator->flags == 0) {
            --iterator;
            if (!iterator.valid()) break;
        }
        
        if (iterator.valid() && iterator->flags > 0) {
            return iterator->timestamp;
        }
    } catch (const nanots_exception&) {
        // Stream doesn't exist or no key frame found
    }
    
    return ts;  // Return the original timestamp as fallback
}

void r_storage_file_reader::_extract_codec_info(const string& metadata,
                                                string& video_codec_name, string& video_codec_parameters,
                                                string& audio_codec_name, string& audio_codec_parameters)
{
    try {
        auto json_data = nlohmann::json::parse(metadata);
        
        if (json_data.contains("video_codec_name")) {
            video_codec_name = json_data["video_codec_name"].get<string>();
        }
        if (json_data.contains("video_codec_parameters")) {
            video_codec_parameters = json_data["video_codec_parameters"].get<string>();
        }
        if (json_data.contains("audio_codec_name")) {
            audio_codec_name = json_data["audio_codec_name"].get<string>();
        }
        if (json_data.contains("audio_codec_parameters")) {
            audio_codec_parameters = json_data["audio_codec_parameters"].get<string>();
        }
    } catch (const nlohmann::json::exception&) {
        // Failed to parse metadata, leave codec info empty
    }
}