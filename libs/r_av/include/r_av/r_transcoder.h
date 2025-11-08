
#ifndef r_av_r_transcoder_h
#define r_av_r_transcoder_h

extern "C"
{
#include <libavcodec/avcodec.h>
}

#include "r_av/r_video_decoder.h"
#include "r_av/r_video_encoder.h"
#include "r_av/r_muxer.h"
#include "r_utils/r_macro.h"

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <functional>
#include <chrono>

namespace r_av
{

class r_transcoder final
{
public:
    R_API r_transcoder(
        const std::string& output_url,
        const std::string& output_format,
        AVCodecID input_codec,
        const std::vector<uint8_t>& input_extradata,
        uint16_t output_width,
        uint16_t output_height,
        AVRational framerate,
        uint32_t initial_bitrate,
        uint32_t max_bitrate,
        uint32_t min_bitrate
    );

    R_API ~r_transcoder();

    R_API r_transcoder(const r_transcoder&) = delete;
    R_API r_transcoder& operator=(const r_transcoder&) = delete;

    // Called from need_data_callback to push frames into the transcoder
    R_API void write_frame(const uint8_t* data, size_t size, int64_t pts);

    // Start/stop transcoding
    R_API void start();
    R_API void stop();

private:
    void _worker_thread();
    void _adjust_bitrate();

    // Threading
    std::thread _worker;
    std::atomic<bool> _running;

    // Input queue
    struct queued_frame {
        std::vector<uint8_t> data;
        int64_t pts;
    };
    std::deque<queued_frame> _input_queue;
    std::mutex _queue_mutex;
    std::condition_variable _queue_cv;
    size_t _max_queue_size;

    // Codec components
    r_video_decoder _decoder;
    r_video_encoder _encoder;
    r_muxer _muxer;

    // Bitrate management
    uint32_t _current_bitrate;
    uint32_t _max_bitrate;
    uint32_t _min_bitrate;

    // Timing & rate control
    std::chrono::steady_clock::time_point _last_bitrate_check;
    size_t _bytes_encoded_this_window;

    // Output parameters
    uint16_t _output_width;
    uint16_t _output_height;
    AVRational _framerate;
    int _profile;
    int _level;
    int64_t _frame_count;
};

}

#endif
