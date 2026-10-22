
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
#include <cstdint>
#include <memory> // For std::shared_ptr

namespace r_av
{

// What the encoder does with one decoded input frame. The transcoder asks
// before it downloads and scales the frame, so a frame the output rate drops
// costs a decode (H.264 needs every frame for its references) and nothing more.
struct r_output_frame_plan
{
    bool keep {false};    // encode this frame at pts; false drops it unscaled
    int64_t repeats {0};  // first re-encode the previous kept frame at pts-repeats .. pts-1
    int64_t pts {0};      // encoder pts for this frame when kept
};

// Fits input frames to the output frame rate. Feed it each decoded frame's pts
// already rescaled to the encoder timebase (1/fps): a frame that lands on a
// tick already filled is dropped, and a short gap of missing ticks is filled by
// repeating the previous frame. A gap longer than max_repeats ticks is a jump
// (the input skipped ahead to catch up with live), not a stall: the frame after
// it is encoded at its own pts with no repeats, so the output jumps with it
// instead of encoding seconds of a held picture on a stream that is behind.
class r_output_frame_selector final
{
public:
    R_API explicit r_output_frame_selector(int64_t max_repeats = INT64_MAX);

    R_API r_output_frame_plan next(int64_t encoder_pts);
    R_API void reset();

private:
    int64_t _max_repeats;
    int64_t _next_pts {-1};
};

class r_transcoder final
{
public:
    R_API r_transcoder(
        const std::string& output_url,
        const std::string& output_format,
        AVCodecID input_codec,
        const std::vector<uint8_t>& input_extradata,
        AVRational input_timebase,
        uint16_t output_width,
        uint16_t output_height,
        AVRational framerate,
        uint32_t initial_bitrate,
        uint32_t max_bitrate,
        uint32_t min_bitrate,
        bool enable_dynamic_bitrate = true,
        const std::string& passphrase = ""
    );

    R_API ~r_transcoder();

    R_API r_transcoder(const r_transcoder&) = delete;
    R_API r_transcoder& operator=(const r_transcoder&) = delete;

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
    bool _enable_dynamic_bitrate;

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

    // Resampling
    AVRational _input_timebase;
    r_output_frame_selector _selector;
    std::shared_ptr<std::vector<uint8_t>> _last_decoded_frame;
};

}

#endif
