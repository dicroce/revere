#ifndef __r_av_r_srt_stats_h
#define __r_av_r_srt_stats_h

#include <cstdint>

namespace r_av
{

/**
 * An SRT sender's link statistics at one moment (libsrt's SRT_TRACEBSTATS): what an uplink that can't keep up looks
 * like from the sender. The packet counts are totals since the connection began, so a window's figure is the
 * difference of two reads. The rest are libsrt's instantaneous values.
 */
struct r_srt_stats
{
    int64_t pkt_sent_total = 0;       // packets sent, first transmissions only
    int64_t pkt_retrans_total = 0;    // packets sent again because the receiver reported them lost
    int64_t pkt_snd_drop_total = 0;   // packets dropped by the sender, too late to be worth sending
    double rtt_ms = 0.0;              // round trip time
    double mbps_send_rate = 0.0;      // what is being sent
    double mbps_bandwidth = 0.0;      // libsrt's estimate of the link's capacity
    int64_t byte_snd_buf = 0;         // bytes waiting in the send buffer
    int64_t ms_snd_buf = 0;           // how much time those bytes are
};

}

#endif
