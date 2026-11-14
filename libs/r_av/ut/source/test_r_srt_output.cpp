#include "test_r_srt_output.h"
#include "r_av/r_srt_output.h"
#include "r_av/r_muxer.h"
#include "r_utils/r_exception.h"

#ifdef R_HAVE_LIBSRT
#include <srt/srt.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif
#include <filesystem>
#include <memory>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>
#endif

using namespace r_av;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_srt_output);

#ifdef R_HAVE_LIBSRT

// A local SRT listener (the ingest's side): accepts one caller and counts what it receives.
struct listener
{
    SRTSOCKET sock = SRT_INVALID_SOCK;
    uint16_t port = 0;
    atomic<long> received{0};
    atomic<bool> accepted{false};
    atomic<bool> stop{false};
    thread worker;

    explicit listener(const string& passphrase)
    {
        srt_startup();
        sock = srt_create_socket();
        int live = SRTT_LIVE;
        srt_setsockflag(sock, SRTO_TRANSTYPE, &live, sizeof(live));
        if(!passphrase.empty())
            srt_setsockflag(sock, SRTO_PASSPHRASE, passphrase.c_str(), (int)passphrase.size());
        // Never blocks for long, so the worker notices `stop` (a blocked srt_accept does not always wake on close).
        int to = 200;
        srt_setsockflag(sock, SRTO_RCVTIMEO, &to, sizeof(to));
        int nosyn = 0;
        srt_setsockflag(sock, SRTO_RCVSYN, &nosyn, sizeof(nosyn));
        sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_port = 0;
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        srt_bind(sock, (sockaddr*)&a, sizeof(a));
        sockaddr_in got;
        int len = sizeof(got);
        srt_getsockname(sock, (sockaddr*)&got, &len);
        port = ntohs(got.sin_port);
        srt_listen(sock, 1);
        worker = thread([this]() {
            int eid = srt_epoll_create();
            int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
            srt_epoll_add_usock(eid, sock, &events);
            while(!stop)
            {
                SRTSOCKET ready[2];
                int n = 2;
                if(srt_epoll_wait(eid, ready, &n, nullptr, nullptr, 200, nullptr, nullptr, nullptr, nullptr) <= 0)
                    continue;
                sockaddr_storage peer;
                int plen = sizeof(peer);
                SRTSOCKET c = srt_accept(sock, (sockaddr*)&peer, &plen);
                if(c == SRT_INVALID_SOCK)
                    continue;
                accepted = true;
                int block = 1;
                srt_setsockflag(c, SRTO_RCVSYN, &block, sizeof(block));
                vector<char> buf(1500);
                while(!stop)
                {
                    int r = srt_recvmsg(c, buf.data(), (int)buf.size());
                    if(r == SRT_ERROR)
                    {
                        if(srt_getlasterror(nullptr) == SRT_EASYNCRCV)
                            continue;
                        break;
                    }
                    if(r == 0)
                        break;
                    received += r;
                }
                srt_close(c);
            }
            srt_epoll_release(eid);
        });
    }
    ~listener()
    {
        stop = true;
        if(worker.joinable())
            worker.join();
        srt_close(sock);
        srt_cleanup();
    }
    string url(const string& extra) const { return "srt://127.0.0.1:" + to_string(port) + "?" + extra; }
};

void test_r_srt_output::test_sends_to_a_listener_and_reports_stats()
{
    RTF_ASSERT(r_srt_output::available());
    listener l("");
    auto out = make_unique<r_srt_output>(r_parse_srt_url(l.url("streamid=abc")));
    vector<uint8_t> data(1316 * 20 + 100, 0x47);
    RTF_ASSERT(out->write(data.data(), (int)data.size()) == (int)data.size());
    for(int i = 0; i < 100 && l.received < (long)data.size(); ++i)
        this_thread::sleep_for(chrono::milliseconds(50));
    RTF_ASSERT(l.accepted);
    RTF_ASSERT(l.received == (long)data.size());
    auto s = out->stats();
    RTF_ASSERT(s.has_value());
    RTF_ASSERT(s->pkt_sent_total >= 21);
    RTF_ASSERT(s->rtt_ms >= 0.0);
    out->close();
    RTF_ASSERT(!out->stats().has_value());
}

void test_r_srt_output::test_a_wrong_passphrase_does_not_connect()
{
    {
        listener l("0123456789abcdef");
        bool threw = false;
        try { r_srt_output o(r_parse_srt_url(l.url("passphrase=wrongwrongwrong"))); }
        catch(const std::exception&) { threw = true; }
        RTF_ASSERT(threw);
    }
    // The right one does (another listener: libsrt's listener holds off a caller it just rejected).
    listener l2("0123456789abcdef");
    r_srt_output ok(r_parse_srt_url(l2.url("passphrase=0123456789abcdef")));
    vector<uint8_t> d(1316, 1);
    RTF_ASSERT(ok.write(d.data(), (int)d.size()) == 1316);
}

void test_r_srt_output::test_a_closed_output_fails_its_writes()
{
    listener l("");
    r_srt_output out(r_parse_srt_url(l.url("")));
    out.close();
    out.close(); // twice is fine
    uint8_t b[10] = {0};
    RTF_ASSERT(out.write(b, 10) < 0);
}

// The whole path: r_muxer writes MPEG-TS, FFmpeg's custom IO hands it to libsrt, a listener receives it, and the
// muxer's stats are the link's, until it is finalized.
void test_r_srt_output::test_a_muxer_sends_mpegts_through_libsrt_and_reads_stats()
{
    listener l("");
    r_muxer m(l.url("streamid=x"), false, "mpegts");
    m.add_video_stream(AVRational{15, 1}, AV_CODEC_ID_H264, 854, 480, AV_PROFILE_H264_MAIN, 41);
    m.use_libsrt();
    m.open();
    RTF_ASSERT(m.srt_stats().has_value());
    vector<uint8_t> nal(3000, 0x11);
    nal[0] = 0; nal[1] = 0; nal[2] = 0; nal[3] = 1; nal[4] = 0x65;
    for(int i = 0; i < 30; ++i)
        m.write_video_frame(nal.data(), nal.size(), i * 66, i * 66, AVRational{1, 1000}, i == 0);
    for(int i = 0; i < 100 && l.received < 90000; ++i)
        this_thread::sleep_for(chrono::milliseconds(50));
    RTF_ASSERT(l.received > 90000);   // 30 frames of 3000 bytes, with TS overhead
    auto s = m.srt_stats();
    RTF_ASSERT(s.has_value());
    RTF_ASSERT(s->pkt_sent_total > 0);
    m.finalize();
    RTF_ASSERT(!m.srt_stats().has_value());
}

void test_r_srt_output::test_a_muxer_that_is_not_srt_has_no_stats()
{
    // use_libsrt() only matters for an srt:// output: a file is as before.
    auto path = (std::filesystem::temp_directory_path() / "r_srt_output_test.ts").string();
    r_muxer m(path, false, "mpegts");
    m.add_video_stream(AVRational{15, 1}, AV_CODEC_ID_H264, 854, 480, AV_PROFILE_H264_MAIN, 41);
    m.use_libsrt();
    m.open();
    RTF_ASSERT(!m.srt_stats().has_value());
    m.finalize();
    std::filesystem::remove(path);
}

#else

void test_r_srt_output::test_a_muxer_sends_mpegts_through_libsrt_and_reads_stats() {}
void test_r_srt_output::test_a_muxer_that_is_not_srt_has_no_stats() {}

void test_r_srt_output::test_sends_to_a_listener_and_reports_stats()
{
    // Built without libsrt: it says so, and an output can't be made.
    RTF_ASSERT(!r_srt_output::available());
    bool threw = false;
    try { r_srt_output o(r_parse_srt_url("srt://127.0.0.1:1")); }
    catch(const std::exception&) { threw = true; }
    RTF_ASSERT(threw);
}
void test_r_srt_output::test_a_wrong_passphrase_does_not_connect() {}
void test_r_srt_output::test_a_closed_output_fails_its_writes() {}

#endif
