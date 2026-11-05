#ifndef r_storage_r_storage_file_h__
#define r_storage_r_storage_file_h__

#include "r_storage/r_storage_file.h"
#include "nanots.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_macro.h"
#include <string>
#include <memory>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>

namespace r_storage
{

enum r_storage_media_type
{
    R_STORAGE_MEDIA_TYPE_VIDEO,
    R_STORAGE_MEDIA_TYPE_AUDIO,
    R_STORAGE_MEDIA_TYPE_ALL,

    R_STORAGE_MEDIA_TYPE_MAX
};

struct r_storage_write_context
{
    std::unique_ptr<write_context> wc;
    std::string codec_name;
    std::string codec_parameters;
};

class r_storage_file final
{
public:
    R_API r_storage_file(const std::string& file_name);

    R_API r_storage_file(const r_storage_file&) = delete;
    R_API r_storage_file(r_storage_file&& other) noexcept = delete;

    R_API ~r_storage_file() noexcept;

    R_API r_storage_file& operator=(const r_storage_file&) = delete;
    R_API r_storage_file& operator=(r_storage_file&& other) noexcept = delete;

    R_API r_storage_write_context create_write_context(
        const std::string& codec_name,
        const r_utils::r_nullable<std::string>& codec_parameters,
        r_storage_media_type media_type
    );

    // Returns the ts the frame was stored at: ts, unless it was clamped
    // (backward jitter) or re-anchored (a backward jump; the correction then
    // applies to every later frame). Anything that refers to this frame by
    // time, such as motion events and detections, should use the returned ts.
    R_API int64_t write_frame(const r_storage_write_context& ctx, r_storage_media_type media_type, const uint8_t* p, size_t size, bool key, int64_t ts, int64_t pts);

    // Newest timestamp write_frame() accepted for each stream (post
    // correction/clamp), -1 before the first. Synchronously that's also the
    // newest one written; with async writes it can be ahead of storage — see
    // written_video_ts().
    R_API int64_t last_video_ts() const { return _last_video_ts; }
    R_API int64_t last_audio_ts() const { return _last_audio_ts; }

    // Async writes, off by default. Once started, write_frame() still corrects
    // the ts and returns it at once, but the frame is copied into a queue that
    // this file's writer thread writes to nanots, so a slow or busy disk no
    // longer stalls the caller. The queue holds at most max_queued_bytes (one
    // frame over that is let into an empty queue); while it's full,
    // write_frame() waits, so frames are never dropped. A write that fails on
    // the writer thread is logged there instead of thrown to the caller.
    //
    // stop_async_writes() writes everything queued, then stops the writer
    // thread; write_frame() is synchronous again. Call it before destroying a
    // write context that may have frames queued. The destructor calls it too.
    R_API void start_async_writes(size_t max_queued_bytes);
    R_API void stop_async_writes() noexcept;

    // Of the video frames that reached storage, the newest one's ts (-1 before
    // the first) and when write_frame() was given it (wall clock; the epoch
    // before the first). The ts is the readable edge of the recording. The
    // arrival time trails "now" by how long frames wait to be written: ~0
    // synchronously, the queue's delay with async writes (a steady backlog
    // shows), and it stops moving while writes fail.
    R_API int64_t written_video_ts() const { return _written_video_ts.load(std::memory_order_acquire); }
    R_API std::chrono::system_clock::time_point written_video_arrival_time() const
    {
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(_written_video_arrival_ms.load(std::memory_order_acquire)));
    }

    R_API static size_t remove_blocks(const std::string& file_name, int64_t start_ts, int64_t end_ts);

    R_API static void allocate(const std::string& file_name, size_t block_size, size_t num_blocks);

private:
    struct _queued_frame
    {
        write_context* wc {nullptr};
        r_storage_media_type media_type {R_STORAGE_MEDIA_TYPE_VIDEO};
        std::vector<uint8_t> data;
        uint32_t flags {0};
        int64_t ts {0};
        int64_t arrival_ms {0};
    };

    // The nanots write itself, on whichever thread does it.
    void _write(write_context& wc, r_storage_media_type media_type, const uint8_t* p, size_t size, uint32_t flags, int64_t ts, int64_t arrival_ms, size_t queued_bytes);
    void _writer_entry();

    std::string _file_name;
    std::unique_ptr<nanots_writer> _writer;
    int64_t _last_video_ts {-1};
    int64_t _last_audio_ts {-1};
    int64_t _video_ts_correction {0};
    int64_t _audio_ts_correction {0};
    int64_t _last_drift_alarm_ms {0};

    std::atomic<int64_t> _written_video_ts {-1};
    std::atomic<int64_t> _written_video_arrival_ms {0};

    // Async writes. _async: write_frame() queues. _writer_running: the writer
    // thread exists (it can still be writing what's queued after _async goes
    // false; write_frame() waits for that, so frames stay in order).
    std::mutex _q_lock;
    std::condition_variable _q_work;   // the writer: a frame, or stop
    std::condition_variable _q_room;   // write_frame(): room, or the writer stopped
    std::deque<_queued_frame> _q;
    size_t _q_bytes {0};
    size_t _q_max_bytes {0};
    bool _async {false};
    bool _writer_running {false};
    bool _q_stop {false};
    bool _q_full_logged {false};
    std::thread _writer_thread;
};

R_API std::pair<int64_t, int64_t> required_file_size_for_retention_hours(int64_t retention_hours, int64_t byte_rate);
R_API std::string human_readable_file_size(double size);

}

#endif