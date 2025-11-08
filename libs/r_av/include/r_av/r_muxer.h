
#ifndef r_mux_r_muxer_h
#define r_mux_r_muxer_h

#include "r_av/r_format_utils.h"
#include "r_utils/r_std_utils.h"
#include "r_utils/r_macro.h"

#include <string>
#include <utility>
#include <cstdint>
#include <memory>
#include <functional>
#include <vector>
#include <map>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/rational.h>
}

extern "C"
{
#include <libavcodec/bsf.h>
}

namespace r_av
{

R_API AVCodecID encoding_to_av_codec_id(const std::string& codec_name);

class r_muxer final
{
public:
    R_API r_muxer(const std::string& path, bool output_to_buffer=false, const std::string& format_name="");
    R_API r_muxer(const r_muxer&) = delete;
    R_API r_muxer(r_muxer&& obj) = delete;
    R_API ~r_muxer();

    R_API r_muxer& operator=(const r_muxer&) = delete;
    R_API r_muxer& operator=(r_muxer&&) = delete;

    R_API void add_video_stream(AVRational frame_rate, AVCodecID codec_id, uint16_t w, uint16_t h, int profile, int level);
    R_API void add_audio_stream(AVCodecID codec_id, uint8_t channels, uint32_t sample_rate);

    R_API void set_video_bitstream_filter(const std::string& filter_name);
    R_API void set_audio_bitstream_filter(const std::string& filter_name);

    R_API void set_video_extradata(const std::vector<uint8_t>& ed);
    R_API void set_audio_extradata(const std::vector<uint8_t>& ed);

    R_API void set_output_option(const std::string& key, const std::string& value);

    R_API void open();

    R_API void write_video_frame(uint8_t* p, size_t size, int64_t input_pts, int64_t input_dts, AVRational input_time_base, bool key);
    R_API void write_audio_frame(uint8_t* p, size_t size, int64_t input_pts, AVRational input_time_base);

    R_API void finalize();

    R_API const uint8_t* buffer() const;
    R_API size_t buffer_size() const;

private:
    std::string _path;
    bool _output_to_buffer;
    std::string _format_name;
    std::map<std::string, std::string> _output_options;
    std::vector<uint8_t> _buffer;

    r_utils::r_std_utils::raii_ptr<AVFormatContext> _fc;
    AVStream* _video_stream; // raw pointers allowed here because they are cleaned up automatically by _fc
    AVStream* _audio_stream;
    bool _needs_finalize;
    r_utils::r_std_utils::raii_ptr<AVBSFContext> _video_bsf;
    r_utils::r_std_utils::raii_ptr<AVBSFContext> _audio_bsf;
};

}

#endif
