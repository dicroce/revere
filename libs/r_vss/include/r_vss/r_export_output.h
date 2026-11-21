#ifndef __r_vss_r_export_output_h
#define __r_vss_r_export_output_h

#include "r_av/r_muxer.h"
#include "r_utils/r_macro.h"
#include <string>

namespace r_vss
{

// The file an export writes. The muxer writes to a tmp_ file in the exports folder, and finish() renames it to the
// name asked for. An export that never wrote its header (every chunk failed: a codec the container can't hold, no
// recording in the range) throws from finish(), so the job reports failed. The tmp_ file is removed when this is
// destroyed unless it was renamed, so a failed export leaves nothing behind.
//
// Declare it before the muxer: the muxer is destroyed first and closes the file, which Windows needs before it can be
// removed.
class r_export_output final
{
public:
    R_API r_export_output(const std::string& exports_path, const std::string& file_name);
    R_API r_export_output(const r_export_output&) = delete;
    R_API r_export_output& operator=(const r_export_output&) = delete;
    R_API ~r_export_output() noexcept;

    R_API const std::string& temp_path() const { return _temp_path; }
    R_API const std::string& output_path() const { return _output_path; }

    // The muxer wrote its header.
    R_API void opened() { _opened = true; }
    R_API bool is_open() const { return _opened; }

    // Why a chunk was skipped. The first one is kept for finish()'s error.
    R_API void skipped(const std::string& why);

    // Finalizes the muxer and renames the tmp_ file. Throws when the header was never written.
    R_API void finish(r_av::r_muxer& muxer);

private:
    std::string _output_path;
    std::string _temp_path;
    bool _opened {false};
    std::string _first_skip;
};

}

#endif
