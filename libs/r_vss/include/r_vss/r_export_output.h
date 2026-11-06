#ifndef __r_vss_r_export_output_h
#define __r_vss_r_export_output_h

#include "r_utils/r_macro.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <string>

namespace r_vss
{

// How much of the recording an export reads at once. Each query's frames are held in memory three times over (read,
// serialized, parsed): five minutes of a 6.5 Mbit/s camera took revere from 32 MB to 712 MB.
constexpr std::chrono::seconds EXPORT_CHUNK {60};

// An export's progress while it runs, from how much of its range is done: at most 99. 100 means its file is in
// place (or it failed), so only the export's worker sets it, after the file is finalized and renamed. (Clients
// download at 100: a 100 set after the last chunk's query, with the file still being written, gave them a 404.)
inline int export_percent(int64_t done_ms, int64_t total_ms)
{
    if(total_ms <= 0 || done_ms <= 0)
        return 0;
    auto p = (int)(done_ms * 100 / total_ms);
    return p > 99 ? 99 : p;
}

// An exception's message for an export's error: its first line (an r_exception's what() goes on with its stack).
R_API std::string export_message(const std::exception& ex);

// Where an export's file is made (r_ws's /export and /transcode_export): a temp file in the exports folder, renamed
// to the asked name only once it's finished. Each chunk of the recording goes in or is skipped, with its reason kept.
//
// An export that ends without its file throws, saying why, so its job is reported as failed rather than complete:
// stopped (revere is shutting down), or nothing could be written (no recording in the range, or every chunk was
// skipped). The temp file never outlives an export that didn't finish.
class r_export_output
{
public:
    R_API r_export_output(const std::string& exports_path, const std::string& file_name);
    R_API ~r_export_output() noexcept;

    r_export_output(const r_export_output&) = delete;
    r_export_output& operator=(const r_export_output&) = delete;

    const std::string& temp_path() const { return _temp_path; }
    const std::string& output_path() const { return _output_path; }

    // Whether a frame of a stream (its storage media type) goes in. A chunk's query starts back at the key frame
    // before the chunk, so the frames from there to the chunk's start went in with the last chunk; a frame no newer
    // than the stream's last one taken is one of those (stored times only increase), and is left out.
    R_API bool take(int stream_id, int64_t ts);

    // A chunk that couldn't be written.
    R_API void skipped(const std::string& reason);
    size_t skipped_chunks() const { return _skipped; }

    // Throws if the export can't be finished: stopped, or nothing written (written: the muxer was opened). range
    // names the export's times, for the message.
    R_API void check(bool written, bool stopped, const std::string& range) const;

    // The finished (finalized) file to its asked name.
    R_API void commit();

private:
    std::string _output_path;
    std::string _temp_path;
    std::map<int, int64_t> _last_ts;
    size_t _skipped {0};
    std::string _last_skip;
    bool _committed {false};
};

}

#endif
