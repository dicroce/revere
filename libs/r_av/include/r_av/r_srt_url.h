#ifndef __r_av_r_srt_url_h
#define __r_av_r_srt_url_h

#include "r_utils/r_macro.h"

#include <cstdint>
#include <string>

namespace r_av
{

/**
 * What an SRT output URL says, for opening the connection ourselves rather than through FFmpeg.
 *
 * Only what a caller needs: srt://host[:port] and the options streamid, passphrase, latency, pbkeylen and
 * mode=caller (FFmpeg's names and units: latency in microseconds, as in `srt://host:port?latency=200000`).
 * Anything else, another scheme, a listener or rendezvous mode, a path or user info, is refused rather than ignored.
 * Values are percent-decoded.
 */
struct r_srt_endpoint
{
    std::string host;
    uint16_t port = 778;
    std::string stream_id;
    std::string passphrase;
    int latency_ms = -1;   // -1: libsrt's default
    int pbkeylen = -1;     // -1: libsrt's default
};

/** Throws r_exception for a URL that is not one of those. */
R_API r_srt_endpoint r_parse_srt_url(const std::string& url);

}

#endif
