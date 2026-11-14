
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
#include <optional>
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

/**
 * An audio stream for the transcoder's output, written as it comes (not re-encoded): AAC with its AudioSpecificConfig
 * as extradata, for an MPEG-TS that carries the camera's audio beside the video.
 */
// A live output with audio holds its video for at most this long while the audio pauses (r_muxer::set_max_interleave_delta).
constexpr int64_t R_TRANSCODER_AUDIO_INTERLEAVE_US = 500000;

struct r_transcoder_audio
{
    AVCodecID codec_id{AV_CODEC_ID_AAC};
    uint8_t channels{1};
    uint32_t sample_rate{0};
    std::vector<uint8_t> extradata;
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
        const std::string& passphrase = "",
        const std::optional<r_transcoder_audio>& audio = std::nullopt,
        bool use_libsrt = false
    );

    R_API ~r_transcoder();

    R_API r_transcoder(const r_transcoder&) = delete;
    R_API r_transcoder& operator=(const r_transcoder&) = delete;

    R_API void write_frame(const uint8_t* data, size_t size, int64_t pts);

    /**
     * One encoded audio frame at its pts (the input timebase, as write_frame's), written to the output's audio stream
     * as it is. Ignored when the transcoder was made without audio. Queued with the video, so the output is written
     * from one thread.
     *
     * A frame whose pts isn't past the last audio frame written (a repeat: a caller's chunks can overlap) is dropped,
     * and counts in audio_frames_dropped(): the muxer would refuse it, and that ended the stream.
     */
    R_API void write_audio_frame(const uint8_t* data, size_t size, int64_t pts);

    /** Whether the output has an audio stream (the constructor's audio). */
    R_API bool has_audio() const { return _has_audio; }

    /** Audio frames not written because they didn't move past the last one (repeats). */
    R_API uint64_t audio_frames_dropped() const { return _audio_dropped; }

    /**
     * True once the worker has stopped on an error (the output closed, say). Nothing more goes out, and writes return
     * at once instead of waiting for room in a queue nothing drains.
     */
    R_API bool failed() const { return _failed; }

    /**
     * The SRT link's statistics now, when the output is an srt:// URL sent through libsrt (the `use_libsrt` argument,
     * where this build has it). Empty otherwise. Safe to call from another thread while it runs.
     */
    R_API std::optional<r_srt_stats> srt_stats() const { return _muxer.srt_stats(); }

    // Start/stop transcoding
    R_API void start();
    R_API void stop();

private:
    void _worker_thread();
    void _adjust_bitrate();

    // Threading
    std::thread _worker;
    std::atomic<bool> _running;
    // Set when the worker has returned, error or not: the queue is drained by no one after it.
    std::atomic<bool> _worker_done{false};
    std::atomic<bool> _failed{false};

    // Input queue
    struct queued_frame {
        std::vector<uint8_t> data;
        int64_t pts;
        bool audio{false};
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

    // Audio, written as it comes (see r_transcoder_audio and write_audio_frame)
    bool _has_audio{false};
    int64_t _last_audio_pts{0};         // the worker's own: the last audio pts written
    bool _audio_written{false};         // the worker's own
    std::atomic<uint64_t> _audio_dropped{0};
};

}

#endif
