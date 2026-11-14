#ifndef __r_av_r_srt_output_h
#define __r_av_r_srt_output_h

#include "r_av/r_srt_stats.h"
#include "r_av/r_srt_url.h"
#include "r_utils/r_macro.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace r_av
{

/**
 * An SRT caller that sends what it is given, through libsrt directly (not FFmpeg's srt protocol, which does not
 * expose the link's statistics). Live mode, as FFmpeg's is: 1316 byte payloads (seven TS packets).
 *
 * Built only where libsrt is: `available()` says whether it was. Without it constructing one throws, and a caller
 * that wants stats falls back to FFmpeg's own output.
 */
class r_srt_output final
{
public:
    /** Whether this build has libsrt. */
    R_API static bool available();

    /** Connects (blocking, 3 s), or throws with libsrt's reason. */
    R_API explicit r_srt_output(const r_srt_endpoint& endpoint);
    R_API r_srt_output(const r_srt_output&) = delete;
    R_API r_srt_output& operator=(const r_srt_output&) = delete;
    R_API ~r_srt_output();

    /** Sends `size` bytes: all of them, or a negative number when the link has failed. */
    R_API int write(const uint8_t* data, int size);

    /** The link's statistics now; empty when they can't be read (closed, or no libsrt). Safe from another thread. */
    R_API std::optional<r_srt_stats> stats() const;

    /** Closes the connection; a write in progress on another thread fails. Safe to call twice. */
    R_API void close();

private:
    struct impl;
    std::unique_ptr<impl> _impl;
};

}

#endif
