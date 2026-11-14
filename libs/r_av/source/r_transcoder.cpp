
#include "r_av/r_transcoder.h"
#include "r_av/r_gop.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_logger.h"
#include <algorithm>

using namespace r_av;
using namespace r_utils;
using namespace std;

r_transcoder::r_transcoder(
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
    bool enable_dynamic_bitrate,
    const std::string& passphrase,
    const std::optional<r_transcoder_audio>& audio,
    uint16_t gop_seconds,
    bool use_libsrt
) :
    _running(false),
    _max_queue_size(30),
    _decoder(input_codec, r_find_best_hw_accel(input_codec)),
    _encoder(),
    _muxer(passphrase.empty() ? output_url : output_url + "&passphrase=" + passphrase, false, output_format),
    _current_bitrate(initial_bitrate),
    _max_bitrate(max_bitrate),
    _min_bitrate(min_bitrate),
    _enable_dynamic_bitrate(enable_dynamic_bitrate),
    _bytes_encoded_this_window(0),
    _output_width(output_width),
    _output_height(output_height),
    _framerate(framerate),
    _profile(AV_PROFILE_H264_MAIN),
    _level(41),
    _frame_count(0),
    _input_timebase(input_timebase),
    // Fill gaps of up to a second; a longer one is a jump, not a stall
    _selector((framerate.num + framerate.den - 1) / framerate.den),
    _last_decoded_frame(nullptr)
{
    // Set extradata on decoder
    if (!input_extradata.empty())
    {
        _decoder.set_extradata(input_extradata);
    }

    // Create encoder
    _encoder = r_video_encoder(
        AV_CODEC_ID_H264,
        initial_bitrate,
        output_width,
        output_height,
        framerate,
        AV_PIX_FMT_YUV420P,
        0,
        r_gop_frames(framerate, gop_seconds),
        _profile,
        _level,
        "veryfast",
        "zerolatency"
    );

    // Setup muxer
    _muxer.add_video_stream(framerate, AV_CODEC_ID_H264, output_width, output_height, _profile, _level);
    _muxer.set_video_extradata(_encoder.get_extradata());
    if (audio && audio->sample_rate > 0)
    {
        _muxer.add_audio_stream(audio->codec_id, audio->channels, audio->sample_rate);
        if (!audio->extradata.empty())
            _muxer.set_audio_extradata(audio->extradata);
        // A camera's audio can pause (or be held back while the stream catches up with live): the picture must not
        // wait the default 10 s for it.
        _muxer.set_max_interleave_delta(R_TRANSCODER_AUDIO_INTERLEAVE_US);
        _has_audio = true;
    }
    if (use_libsrt)
        _muxer.use_libsrt();
    _muxer.open();
}

r_transcoder::~r_transcoder()
{
    stop();
}

void r_transcoder::start()
{
    if (_running.exchange(true))
        return; // Already running

    _last_bitrate_check = std::chrono::steady_clock::now();
    _bytes_encoded_this_window = 0;
    _frame_count = 0;
    _selector.reset();
    _last_decoded_frame = nullptr;
    _worker_done = false;
    _failed = false;
    _audio_written = false;
    _last_audio_pts = 0;

    _worker = std::thread(&r_transcoder::_worker_thread, this);
}

void r_transcoder::stop()
{
    if (!_running.exchange(false))
        return; // Not running

    _queue_cv.notify_all();

    if (_worker.joinable())
        _worker.join();
}

void r_transcoder::write_frame(const uint8_t* data, size_t size, int64_t pts)
{
    std::unique_lock<std::mutex> lock(_queue_mutex);

    // Block if queue is full (backpressure), but never on a worker that has stopped: nothing would drain it.
    _queue_cv.wait(lock, [this] {
        return _input_queue.size() < _max_queue_size || !_running || _worker_done;
    });

    if (!_running || _worker_done)
        return;

    queued_frame frame;
    frame.data.resize(size);
    memcpy(frame.data.data(), data, size);
    frame.pts = pts;

    _input_queue.push_back(std::move(frame));
    _queue_cv.notify_one();
}

void r_transcoder::write_audio_frame(const uint8_t* data, size_t size, int64_t pts)
{
    if (!_has_audio || size == 0)
        return;

    std::unique_lock<std::mutex> lock(_queue_mutex);

    _queue_cv.wait(lock, [this] {
        return _input_queue.size() < _max_queue_size || !_running || _worker_done;
    });

    if (!_running || _worker_done)
        return;

    queued_frame frame;
    frame.data.assign(data, data + size);
    frame.pts = pts;
    frame.audio = true;

    _input_queue.push_back(std::move(frame));
    _queue_cv.notify_one();
}

void r_transcoder::_worker_thread()
{
    try
    {
        AVRational encoder_tb;
        encoder_tb.num = _framerate.den;
        encoder_tb.den = _framerate.num;

        while (_running)
        {
            queued_frame frame;

            // 1. Get next frame from input queue
            {
                std::unique_lock<std::mutex> lock(_queue_mutex);

                // Wait for data or shutdown
                _queue_cv.wait(lock, [this] {
                    return !_input_queue.empty() || !_running;
                });

                if (!_running && _input_queue.empty())
                    break;

                if (_input_queue.empty())
                    continue;

                frame = std::move(_input_queue.front());
                _input_queue.pop_front();
            }

            // Notify write_frame() that space is available
            _queue_cv.notify_one();

            // Audio goes to the output as it is, in the order it came with the video. A frame that doesn't move past
            // the last one written is a repeat (a caller's chunks can overlap): the muxer would refuse it ("non
            // monotonically increasing dts"), which ended the stream, so it goes here instead.
            if (frame.audio)
            {
                if (_audio_written && frame.pts <= _last_audio_pts)
                {
                    ++_audio_dropped;
                    continue;
                }
                _muxer.write_audio_frame(frame.data.data(), frame.data.size(), frame.pts, _input_timebase);
                _last_audio_pts = frame.pts;
                _audio_written = true;
                continue;
            }

            // 2. Decode
            _decoder.attach_buffer(frame.data.data(), frame.data.size());
            auto decode_state = _decoder.decode();

            if (decode_state != R_CODEC_STATE_HAS_OUTPUT &&
                decode_state != R_CODEC_STATE_AGAIN_HAS_OUTPUT)
                continue;

            // 3. Fit the frame to the output rate, BEFORE it is downloaded and
            // scaled: at a 30 fps camera and 15 fps output, half the frames
            // are dropped, and scaling those first cost more than the decode.
            auto plan = _selector.next(av_rescale_q(frame.pts, _input_timebase, encoder_tb));
            if (!plan.keep)
                continue;

            // 4. Fill a gap in the input by repeating the previous frame
            for (int64_t pts = plan.pts - plan.repeats; pts < plan.pts; ++pts)
            {
                if (_last_decoded_frame)
                {
                    _encoder.attach_buffer(_last_decoded_frame->data(), _last_decoded_frame->size(), pts);
                    while(true)
                    {
                        auto encode_state = _encoder.encode();
                        if (encode_state == R_CODEC_STATE_HAS_OUTPUT)
                        {
                            auto pi = _encoder.get();
                            _muxer.write_video_frame(pi.data, pi.size, pi.pts, pi.dts, pi.time_base, pi.key);
                            _bytes_encoded_this_window += pi.size;
                        }
                        else break;
                    }
                }
            }

            // 5. Get decoded frame (with scaling)
            auto decoded = _decoder.get(
                AV_PIX_FMT_YUV420P,
                _output_width,
                _output_height,
                1
            );

            // Encode current frame
            _encoder.attach_buffer(decoded->data(), decoded->size(), plan.pts);
            
            while(true)
            {
                auto encode_state = _encoder.encode();

                if (encode_state == R_CODEC_STATE_HAS_OUTPUT)
                {
                    auto pi = _encoder.get();
                    _muxer.write_video_frame(pi.data, pi.size, pi.pts, pi.dts, pi.time_base, pi.key);
                    _bytes_encoded_this_window += pi.size;
                }
                else break;
            }

                // Check bitrate adjustment
                if (_enable_dynamic_bitrate)
                {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        now - _last_bitrate_check
                    ).count();

                    if (elapsed >= 5)
                    {
                        _adjust_bitrate();
                        _last_bitrate_check = now;
                        _bytes_encoded_this_window = 0;
                    }
                }

            _last_decoded_frame = decoded;
        }

        // Flush encoder
        while (_encoder.flush() == R_CODEC_STATE_HAS_OUTPUT)
        {
            auto pi = _encoder.get();
            _muxer.write_video_frame(pi.data, pi.size, pi.pts, pi.dts, pi.time_base, pi.key);
        }

        _muxer.finalize();
    }
    catch (const std::exception& ex)
    {
        R_LOG_ERROR("Transcoder worker thread error: %s", ex.what());
        _failed = true;
    }

    // Writers waiting for room in the queue: nothing drains it now.
    {
        std::lock_guard<std::mutex> lock(_queue_mutex);
        _worker_done = true;
    }
    _queue_cv.notify_all();
}

r_output_frame_selector::r_output_frame_selector(int64_t max_repeats) :
    _max_repeats(max_repeats)
{
}

r_output_frame_plan r_output_frame_selector::next(int64_t encoder_pts)
{
    r_output_frame_plan plan;

    if (_next_pts == -1)
        _next_pts = encoder_pts;

    // Behind the output clock: this tick is already filled, so drop the frame
    if (encoder_pts < _next_pts)
        return plan;

    plan.keep = true;
    auto gap = encoder_pts - _next_pts;
    plan.repeats = (gap <= _max_repeats) ? gap : 0;
    plan.pts = encoder_pts;
    _next_pts = encoder_pts + 1;
    return plan;
}

void r_output_frame_selector::reset()
{
    _next_pts = -1;
}

void r_transcoder::_adjust_bitrate()
{
    // Calculate actual bitrate over the last window
    uint32_t actual_bitrate = static_cast<uint32_t>((_bytes_encoded_this_window * 8) / 5); // bits per second

    // Calculate how far we are from target
    float ratio = (float)actual_bitrate / (float)_current_bitrate;

    uint32_t new_bitrate = _current_bitrate;

    // If we're consistently under target, we can increase
    if (ratio < 0.85f && _current_bitrate < _max_bitrate)
    {
        // Increase by 25%
        new_bitrate = (std::min)(_max_bitrate,
                                 (uint32_t)(_current_bitrate * 1.25f));
    }
    // If we're over target, we need to decrease
    else if (ratio > 1.15f)
    {
        // Decrease by 30%
        new_bitrate = (std::max)(_min_bitrate,
                                 (uint32_t)(_current_bitrate * 0.70f));
    }

    // Only adjust if bitrate changed significantly (>10%)
    if (std::abs((int)new_bitrate - (int)_current_bitrate) > (int)(_current_bitrate * 0.10f))
    {
        R_LOG_INFO("Adjusting bitrate: %u -> %u (actual: %u)", _current_bitrate, new_bitrate, actual_bitrate);

        // Update encoder bitrate dynamically
        _encoder.set_bitrate(new_bitrate);
        _current_bitrate = new_bitrate;
    }
}
