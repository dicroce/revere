#include "r_storage/r_storage_file_reader.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_blob_tree.h"
#include "r_utils/r_logger.h"
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
    
    // Track where the video keyframe backup landed so audio can be aligned to it.
    int64_t video_actual_start_ts = start_ts;

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

            if (video_iterator.valid())
                video_actual_start_ts = video_iterator->timestamp;

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

        // Query audio frames if needed.
        // Start audio at the same timestamp as the video keyframe so both streams
        // are aligned — without this, audio starts at start_ts while video starts
        // potentially seconds earlier, producing a black-video / audio-only gap.
        if (media_type == R_STORAGE_MEDIA_TYPE_AUDIO || media_type == R_STORAGE_MEDIA_TYPE_ALL) {
            nanots_iterator audio_iterator(nanots_file_name, "audio");
            audio_iterator.find(video_actual_start_ts);
            
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

        // If find() failed (e.g. ts is slightly beyond the latest stored data),
        // fall back to searching within a 5-minute window before ts.
        if (!iterator.valid()) {
            static const int64_t five_minutes_ms = 5LL * 60 * 1000;
            int64_t fallback_ts = ts - five_minutes_ms;
            iterator.find(fallback_ts);

            if (iterator.valid()) {
                // Walk forward to find the last key frame at or before ts
                int64_t best_key_ts = -1;
                while (iterator.valid() && iterator->timestamp <= ts) {
                    if (iterator->flags > 0)
                        best_key_ts = iterator->timestamp;
                    ++iterator;
                }

                // Reposition at the best key frame found
                if (best_key_ts >= 0)
                    iterator.find(best_key_ts);
            }
        }

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
    } catch (const nanots_exception& e) {
        R_LOG_ERROR("query_key: nanots error: %s", e.what());
    } catch (const std::exception& e) {
        R_LOG_ERROR("query_key: error: %s", e.what());
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
        auto video_segments = _reader->query_contiguous_segments("video", start_ts, NANOTS_SEC_KEY_UNSET, end_ts, INT64_MAX);
        for (const auto& seg : video_segments) {
            segments.push_back(make_pair(seg.start_timestamp, (seg.end_timestamp==0)?duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count():seg.end_timestamp));
        }
    } catch (const nanots_exception&) {
        try {
            // Fallback to audio stream if video doesn't exist
            auto audio_segments = _reader->query_contiguous_segments("audio", start_ts, NANOTS_SEC_KEY_UNSET, end_ts, INT64_MAX);
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
    return query_segments(start_ts, end_ts);
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

    auto check_stream = [&](const std::string& stream_tag) {
        try {
            nanots_iterator it(nanots_file_name, stream_tag);
            it.seek_end();
            if (it.valid()) {
                auto ts = it->timestamp;
                if (result.is_null() || ts > result.value())
                    result.set_value(ts);
            }
        } catch (const nanots_exception&) {}
    };

    check_stream("video");
    check_stream("audio");

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