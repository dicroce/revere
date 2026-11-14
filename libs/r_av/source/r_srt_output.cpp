#include "r_av/r_srt_output.h"
#include "r_utils/r_exception.h"

#include <atomic>
#include <cstring>
#include <string>

#ifdef R_HAVE_LIBSRT
#include <srt/srt.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#endif
#endif

using namespace r_utils;
using namespace std;

namespace r_av
{

#ifdef R_HAVE_LIBSRT

// The live payload FFmpeg's srt protocol sends: seven 188 byte TS packets.
static const int PAYLOAD = 1316;
static const int CONNECT_TIMEOUT_MS = 3000;
// A write that waits this long on a full send buffer has failed: the transcoder must not hang on it.
static const int SEND_TIMEOUT_MS = 3000;

struct r_srt_output::impl
{
    SRTSOCKET sock = SRT_INVALID_SOCK;
    atomic<bool> closed{false};
};

bool r_srt_output::available() { return true; }

static void _check(int res, const char* what)
{
    if(res == SRT_ERROR)
        R_THROW(("SRT %s failed: %s", what, srt_getlasterror_str()));
}

r_srt_output::r_srt_output(const r_srt_endpoint& e) : _impl(new impl())
{
    srt_startup();
    try
    {
        _impl->sock = srt_create_socket();
        if(_impl->sock == SRT_INVALID_SOCK)
            R_THROW(("SRT socket failed: %s", srt_getlasterror_str()));
        auto s = _impl->sock;

        int live = SRTT_LIVE;
        _check(srt_setsockflag(s, SRTO_TRANSTYPE, &live, sizeof(live)), "set transtype");
        int payload = PAYLOAD;
        _check(srt_setsockflag(s, SRTO_PAYLOADSIZE, &payload, sizeof(payload)), "set payload size");
        int conn = CONNECT_TIMEOUT_MS;
        _check(srt_setsockflag(s, SRTO_CONNTIMEO, &conn, sizeof(conn)), "set connect timeout");
        int snd = SEND_TIMEOUT_MS;
        _check(srt_setsockflag(s, SRTO_SNDTIMEO, &snd, sizeof(snd)), "set send timeout");
        if(!e.stream_id.empty())
            _check(srt_setsockflag(s, SRTO_STREAMID, e.stream_id.c_str(), (int)e.stream_id.size()), "set streamid");
        if(!e.passphrase.empty())
            _check(srt_setsockflag(s, SRTO_PASSPHRASE, e.passphrase.c_str(), (int)e.passphrase.size()), "set passphrase");
        if(e.pbkeylen > 0)
        {
            int k = e.pbkeylen;
            _check(srt_setsockflag(s, SRTO_PBKEYLEN, &k, sizeof(k)), "set pbkeylen");
        }
        if(e.latency_ms >= 0)
        {
            int l = e.latency_ms;
            _check(srt_setsockflag(s, SRTO_LATENCY, &l, sizeof(l)), "set latency");
        }

        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        auto port = to_string(e.port);
        if(getaddrinfo(e.host.c_str(), port.c_str(), &hints, &res) != 0 || !res)
            R_THROW(("SRT could not resolve the host."));
        int rc = srt_connect(s, res->ai_addr, (int)res->ai_addrlen);
        freeaddrinfo(res);
        _check(rc, "connect");
    }
    catch(...)
    {
        close();
        srt_cleanup();
        throw;
    }
}

r_srt_output::~r_srt_output()
{
    close();
    srt_cleanup();
}

int r_srt_output::write(const uint8_t* data, int size)
{
    if(_impl->closed.load())
        return -1;
    int sent = 0;
    while(sent < size)
    {
        int n = size - sent < PAYLOAD ? size - sent : PAYLOAD;
        if(srt_sendmsg2(_impl->sock, reinterpret_cast<const char*>(data) + sent, n, nullptr) == SRT_ERROR)
            return -1;
        sent += n;
    }
    return sent;
}

optional<r_srt_stats> r_srt_output::stats() const
{
    if(_impl->closed.load())
        return nullopt;
    SRT_TRACEBSTATS p;
    memset(&p, 0, sizeof(p));
    if(srt_bistats(_impl->sock, &p, 0, 1) == SRT_ERROR)
        return nullopt;
    r_srt_stats s;
    s.pkt_sent_total = p.pktSentTotal;
    s.pkt_retrans_total = p.pktRetransTotal;
    s.pkt_snd_drop_total = p.pktSndDropTotal;
    s.rtt_ms = p.msRTT;
    s.mbps_send_rate = p.mbpsSendRate;
    s.mbps_bandwidth = p.mbpsBandwidth;
    s.byte_snd_buf = p.byteSndBuf;
    s.ms_snd_buf = p.msSndBuf;
    return s;
}

void r_srt_output::close()
{
    if(_impl && !_impl->closed.exchange(true) && _impl->sock != SRT_INVALID_SOCK)
        srt_close(_impl->sock);
}

#else

struct r_srt_output::impl {};

bool r_srt_output::available() { return false; }

r_srt_output::r_srt_output(const r_srt_endpoint&)
{
    R_THROW(("This build has no libsrt."));
}

r_srt_output::~r_srt_output() {}
int r_srt_output::write(const uint8_t*, int) { return -1; }
optional<r_srt_stats> r_srt_output::stats() const { return nullopt; }
void r_srt_output::close() {}

#endif

}
