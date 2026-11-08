#include "r_storage/r_storage_file.h"
#include "r_utils/r_exception.h"
#include "r_utils/r_string_utils.h"
#include "r_utils/r_logger.h"
#include "r_utils/3rdparty/json/json.h"
#include <memory>
#include <chrono>
#include <algorithm>

using namespace r_utils;
using namespace r_storage;
using namespace std;

r_storage_file::r_storage_file(const string& file_name) :
    _file_name(file_name)
{
    auto base_name = file_name.substr(0, (file_name.find_last_of('.')));
    
    // Use .nts extension for nanots files
    auto nanots_file_name = base_name + ".nts";

    _writer = make_unique<nanots_writer>(nanots_file_name, true);
}

r_storage_file::~r_storage_file() noexcept
{
    stop_async_writes();
}

r_storage_write_context r_storage_file::create_write_context(
    const string& codec_name,
    const r_nullable<string>& codec_parameters,
    r_storage_media_type media_type
)
{
    r_storage_write_context ctx;

    // Create JSON metadata containing codec information
    nlohmann::json metadata_json;
    metadata_json[(media_type == R_STORAGE_MEDIA_TYPE_VIDEO)?"video_codec_name":"audio_codec_name"] = codec_name;
    metadata_json[(media_type == R_STORAGE_MEDIA_TYPE_VIDEO)?"video_codec_parameters":"audio_codec_parameters"] = (!codec_parameters.is_null()) ? codec_parameters.value() : "";

    string metadata_str = metadata_json.dump();

    // Create separate write contexts for video and audio streams
    if(media_type == R_STORAGE_MEDIA_TYPE_VIDEO)
        ctx.wc = make_unique<write_context>(_writer->create_write_context("video", metadata_str));
    else
        ctx.wc = make_unique<write_context>(_writer->create_write_context("audio", metadata_str));

    return ctx;
}

// Backward source-timestamp movement up to this size is treated as normal
// jitter/reordering and clamped to last + 1 for that frame only. Anything
// larger is a discontinuity (camera reboot, NTP step, RTP base reset) and is
// re-anchored instead — see write_frame.
static const int64_t MAX_BACKWARD_TS_WOBBLE_MS = 5000;

// If the written video timeline drifts this far from the wall clock, scream.
// The write path is otherwise silent about this failure mode: writes succeed,
// receiving_video() is true, but time-based queries can't see new video.
static const int64_t TIMELINE_DRIFT_ALARM_MS = 30000;
static const int64_t TIMELINE_DRIFT_ALARM_PERIOD_MS = 60000;

static int64_t _now_epoch_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t r_storage_file::write_frame(const r_storage_write_context& ctx, r_storage_media_type media_type, const uint8_t* p, size_t size, bool key, int64_t ts, int64_t pts)
{
    if(media_type >= R_STORAGE_MEDIA_TYPE_ALL)
        R_THROW(("Invalid storage media type."));

    // nanots requires strictly increasing ts. A non-increasing ts within
    // MAX_BACKWARD_TS_WOBBLE_MS of the last write is normal source jitter and
    // is clamped to last + 1 for THIS frame only. A larger backward jump is a
    // source discontinuity (camera reboot, NTP step, RTP base reset — observed
    // in the wild as a silent pts jump with no reconnect) and must NOT be
    // clamped: per-frame clamping wedges the recording into a last+1ms
    // flatline until the source timestamps catch back up — for a multi-hour
    // jump that is days, during which the readable timeline advances at
    // ~frame-rate milliseconds per real second and time-based queries (/jpg,
    // /webp, playback) find no recent video. Instead, re-anchor the stream at
    // the wall clock and carry the offset forward on subsequent frames so the
    // timeline keeps advancing at the source's cadence.
    const bool is_video = (media_type == R_STORAGE_MEDIA_TYPE_VIDEO);
    int64_t& correction = is_video ? _video_ts_correction : _audio_ts_correction;
    int64_t& last_ts = is_video ? _last_video_ts : _last_audio_ts;

    int64_t corrected = ts + correction;

    if(last_ts != -1 && corrected <= last_ts)
    {
        int64_t jump = last_ts - corrected;
        if(jump > MAX_BACKWARD_TS_WOBBLE_MS)
        {
            // Anchor at the wall clock (never behind the data already written,
            // so monotonicity survives even if the file is ahead of real time).
            int64_t anchor = (std::max)(_now_epoch_ms(), last_ts + 1);
            R_LOG_ERROR("storage ts discontinuity (%s): source ts jumped backwards %lld ms "
                        "(incoming ts %lld, correction %lld, last written %lld); re-anchoring at %lld",
                        is_video ? "video" : "audio", (long long)jump, (long long)ts,
                        (long long)correction, (long long)last_ts, (long long)anchor);
            correction = anchor - ts;
            corrected = anchor;
        }
        else
            corrected = last_ts + 1;
    }

    last_ts = corrected;

    // Watchdog: on video key frames, compare the written timeline against the
    // wall clock. Catches any mechanism that detaches the two — writes still
    // succeed, so storage_write_lag_ms() and receiving_video() stay healthy.
    if(is_video && key)
    {
        int64_t now_ms = _now_epoch_ms();
        int64_t drift = now_ms - corrected;
        if((drift > TIMELINE_DRIFT_ALARM_MS || drift < -TIMELINE_DRIFT_ALARM_MS) &&
           (now_ms - _last_drift_alarm_ms) > TIMELINE_DRIFT_ALARM_PERIOD_MS)
        {
            _last_drift_alarm_ms = now_ms;
            R_LOG_ERROR("storage timeline drift: written video ts %lld is %lld ms %s the wall clock "
                        "— recent video is invisible to time-based queries",
                        (long long)corrected,
                        (long long)(drift < 0 ? -drift : drift),
                        (drift > 0) ? "behind" : "ahead of");
        }
    }

    uint32_t flags = key ? 1 : 0;
    const int64_t arrival_ms = _now_epoch_ms();

    {
        unique_lock<mutex> g(_q_lock);

        // While the queue is full, wait for the writer (never drop). While a
        // stop is still writing the queue, wait too: a synchronous write must
        // not overtake queued frames.
        auto room = [&]() { return _async && (_q.empty() || _q_bytes + size <= _q_max_bytes); };
        if(_writer_running && !room())
        {
            if(_async && !_q_full_logged)
            {
                _q_full_logged = true;
                R_LOG_WARNING("storage writes to %s are %zu bytes behind (queue full); recording waits for the disk",
                              _file_name.c_str(), _q_bytes);
            }
            _q_room.wait(g, [&]() { return room() || !_writer_running; });
        }

        if(_async)
        {
            _queued_frame f;
            f.wc = ctx.wc.get();
            f.media_type = media_type;
            f.data.assign(p, p + size);
            f.flags = flags;
            f.ts = corrected;
            f.arrival_ms = arrival_ms;
            _q_bytes += size;
            _q.push_back(std::move(f));
            g.unlock();
            _q_work.notify_one();
            return corrected;
        }
    }

    _write(*ctx.wc, media_type, p, size, flags, corrected, arrival_ms, 0);

    return corrected;
}

void r_storage_file::_write(write_context& wc, r_storage_media_type media_type, const uint8_t* p, size_t size, uint32_t flags, int64_t ts, int64_t arrival_ms, size_t queued_bytes)
{
    // A write is normally a memcpy into a mapped block (microseconds). A slow
    // one is a block transition or a page that had to come from disk — on a
    // busy recording disk, hundreds of ms.
    auto write_start = std::chrono::steady_clock::now();
    _writer->write(wc, p, size, flags, ts);
    auto write_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - write_start).count();
    if(write_ms > 50)
        R_LOG_WARNING("slow nanots write: %lld ms (media_type=%d, ts=%lld, %zu bytes queued behind it)",
                      (long long)write_ms, (int)media_type, (long long)ts, queued_bytes);

    if(media_type == R_STORAGE_MEDIA_TYPE_VIDEO)
    {
        _written_video_ts.store(ts, std::memory_order_release);
        _written_video_arrival_ms.store(arrival_ms, std::memory_order_release);
    }
}

void r_storage_file::start_async_writes(size_t max_queued_bytes)
{
    lock_guard<mutex> g(_q_lock);
    if(_writer_running)
        return;
    _q_max_bytes = max_queued_bytes;
    _q_stop = false;
    _q_full_logged = false;
    _writer_thread = thread(&r_storage_file::_writer_entry, this);
    _writer_running = true;
    _async = true;
}

void r_storage_file::stop_async_writes() noexcept
{
    {
        lock_guard<mutex> g(_q_lock);
        if(!_writer_running)
            return;
        _async = false;
        _q_stop = true;
    }
    _q_work.notify_one();

    try
    {
        if(_writer_thread.joinable())
            _writer_thread.join();
    }
    catch(...) {}

    {
        lock_guard<mutex> g(_q_lock);
        _writer_running = false;
        _q_stop = false;
    }
    _q_room.notify_all();
}

void r_storage_file::_writer_entry()
{
    for(;;)
    {
        _queued_frame f;
        size_t behind = 0;
        {
            unique_lock<mutex> g(_q_lock);
            _q_work.wait(g, [&]() { return !_q.empty() || _q_stop; });
            if(_q.empty())
                return;  // stopping, and everything is written
            f = std::move(_q.front());
            _q.pop_front();
            _q_bytes -= f.data.size();
            behind = _q_bytes;
            if(_q.empty() && _q_full_logged)
            {
                _q_full_logged = false;
                R_LOG_INFO("storage writes to %s caught up", _file_name.c_str());
            }
        }
        _q_room.notify_all();

        try
        {
            _write(*f.wc, f.media_type, f.data.data(), f.data.size(), f.flags, f.ts, f.arrival_ms, behind);
        }
        catch(const std::exception& e)
        {
            R_LOG_ERROR("storage write to %s failed (media_type=%d, ts=%lld): %s",
                        _file_name.c_str(), (int)f.media_type, (long long)f.ts, e.what());
        }
        catch(...)
        {
            R_LOG_ERROR("storage write to %s failed (media_type=%d, ts=%lld): unknown error",
                        _file_name.c_str(), (int)f.media_type, (long long)f.ts);
        }
    }
}

size_t r_storage_file::remove_blocks(const std::string& file_name, int64_t start_ts, int64_t end_ts)
{
    size_t removed_count = 0;
    
    try {
        nanots_writer::free_blocks(file_name, "video", start_ts, NANOTS_SEC_KEY_UNSET, end_ts, INT64_MAX);
        removed_count++;
    } catch(const nanots_exception&) {
        // Video stream might not exist or have blocks in this range
    }

    try {
        nanots_writer::free_blocks(file_name, "audio", start_ts, NANOTS_SEC_KEY_UNSET, end_ts, INT64_MAX);
        removed_count++;
    } catch(const nanots_exception&) {
        // Audio stream might not exist or have blocks in this range
    }
    
    return removed_count;
}

void r_storage_file::allocate(const std::string& file_name, size_t block_size, size_t num_blocks)
{
    auto base_name = file_name.substr(0, (file_name.find_last_of('.')));
    auto nanots_file_name = base_name + ".nts";
    
    nanots_writer::allocate(nanots_file_name, static_cast<uint32_t>(block_size), static_cast<uint32_t>(num_blocks));
}

// Free functions (moved from r_storage_file static methods)  
pair<int64_t, int64_t> r_storage::required_file_size_for_retention_hours(int64_t retention_hours, int64_t byte_rate)
{
    // Note: windows mmap implementation requires mapped regions to be a multiple of 65536.
    const int64_t TEN_MB_FILE = 10485760; // 10 MiB
    const int64_t FUDGE_FACTOR = 2;

    int64_t natural_byte_size = (byte_rate * 60 * 60 * retention_hours);

    int64_t num_blocks = (natural_byte_size / TEN_MB_FILE) + FUDGE_FACTOR;

    return make_pair(num_blocks, TEN_MB_FILE);
}

string r_storage::human_readable_file_size(double size)
{
    int i = 0;
    const char* units[] = {"bytes", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    while (size > 1024) {
        size /= 1024;
        i++;
    }

    if(i > 8)
        R_THROW(("Size is too big!"));

    return r_string_utils::format("%.2f %s", size, units[i]);
}
