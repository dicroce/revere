
#include "r_vss/r_camera_source_info.h"
#include "r_utils/r_blob_tree.h"
#include "r_storage/r_storage_file.h"
#include "r_storage/r_storage_file_reader.h"
#include "r_pipeline/r_stream_info.h"

using namespace std;

r_vss::camera_source_info r_vss::read_camera_source_info(const string& storage_path)
{
    camera_source_info out;
    try
    {
        r_storage::r_storage_file_reader sf(storage_path);
        auto last_ts = sf.last_ts();
        if(last_ts.is_null())
            return out;

        auto key_bt_buf = sf.query_key(r_storage::R_STORAGE_MEDIA_TYPE_VIDEO, last_ts.value());
        uint32_t version = 0;
        auto bt = r_utils::r_blob_tree::deserialize(key_bt_buf.data(), key_bt_buf.size(), version);

        if(!bt.has_key("video_codec_name") || !bt.has_key("video_codec_parameters"))
            return out;
        auto codec = bt["video_codec_name"].get_string();
        auto params = bt["video_codec_parameters"].get_string();

        out.framerate = r_pipeline::get_video_codec_framerate(params);

        if(codec == "h264")
        {
            auto sps = r_pipeline::get_h264_sps(params);
            if(!sps.is_null())
            {
                auto info = r_pipeline::parse_h264_sps(sps.value());
                out.resolution.set_value({info.width, info.height});
            }
        }
        else if(codec == "h265")
        {
            auto sps = r_pipeline::get_h265_sps(params);
            if(!sps.is_null())
            {
                auto info = r_pipeline::parse_h265_sps(sps.value());
                out.resolution.set_value({info.width, info.height});
            }
        }
    }
    catch(...) { /* swallow — stream info is best-effort metadata */ }
    return out;
}
