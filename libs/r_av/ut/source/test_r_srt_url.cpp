#include "test_r_srt_url.h"
#include "r_av/r_srt_url.h"
#include "r_utils/r_exception.h"

using namespace r_av;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_srt_url);

static bool _refused(const string& url)
{
    try { r_parse_srt_url(url); }
    catch(const std::exception&) { return true; }
    return false;
}

void test_r_srt_url::test_cloudflares_url()
{
    // What the transcoder is given: the URL, then "&passphrase=" and the passphrase.
    auto e = r_parse_srt_url("srt://live.cloudflare.com:778?streamid=abc123&passphrase=0123456789abcdef");
    RTF_ASSERT(e.host == "live.cloudflare.com");
    RTF_ASSERT(e.port == 778);
    RTF_ASSERT(e.stream_id == "abc123");
    RTF_ASSERT(e.passphrase == "0123456789abcdef");
    RTF_ASSERT(e.latency_ms == -1);
    RTF_ASSERT(e.pbkeylen == -1);
}

void test_r_srt_url::test_the_options_a_caller_needs()
{
    auto e = r_parse_srt_url("SRT://Example.com?streamid=s&latency=200000&pbkeylen=32&mode=caller&LATENCY=300000");
    RTF_ASSERT(e.host == "Example.com");
    RTF_ASSERT(e.port == 778);            // SRT's customary port when the URL has none
    RTF_ASSERT(e.latency_ms == 300);      // microseconds in the URL (FFmpeg's unit), milliseconds for libsrt
    RTF_ASSERT(e.pbkeylen == 32);
    auto none = r_parse_srt_url("srt://h:1");
    RTF_ASSERT(none.port == 1 && none.stream_id.empty() && none.passphrase.empty());
}

void test_r_srt_url::test_percent_encoding()
{
    auto e = r_parse_srt_url("srt://h:9?streamid=a%3Db%26c&passphrase=p%20q");
    RTF_ASSERT(e.stream_id == "a=b&c");
    RTF_ASSERT(e.passphrase == "p q");
}

void test_r_srt_url::test_what_is_refused()
{
    RTF_ASSERT(_refused("http://live.cloudflare.com:778"));
    RTF_ASSERT(_refused("srt://"));
    RTF_ASSERT(_refused("srt://:778"));
    RTF_ASSERT(_refused("srt://user@h:1"));
    RTF_ASSERT(_refused("srt://h:1/path"));
    RTF_ASSERT(_refused("srt://h:1?streamid=x#f"));
    RTF_ASSERT(_refused("srt://h:0"));
    RTF_ASSERT(_refused("srt://h:70000"));
    RTF_ASSERT(_refused("srt://h:x"));
    // A listener or rendezvous would have the gateway serve the camera.
    RTF_ASSERT(_refused("srt://h:1?mode=listener"));
    RTF_ASSERT(_refused("srt://h:1?mode=rendezvous"));
    // Options a caller doesn't need are refused, not ignored.
    RTF_ASSERT(_refused("srt://h:1?maxbw=1"));
    RTF_ASSERT(_refused("srt://h:1?streamid"));
    RTF_ASSERT(_refused("srt://h:1?latency=abc"));
    RTF_ASSERT(_refused("srt://h:1?streamid=%zz"));
    RTF_ASSERT(_refused("srt://h:1?streamid=%4"));
    RTF_ASSERT(_refused("srt://h:1?streamid=%"));
}
