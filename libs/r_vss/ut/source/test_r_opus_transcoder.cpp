#include "test_r_opus_transcoder.h"
#include "r_vss/r_opus_transcoder.h"
#include "r_vss/r_stream_keeper.h"
#include "r_av/r_audio_encoder.h"
#include "r_pipeline/r_gst_source.h"
#include "r_pipeline/r_stream_info.h"
#include "r_utils/r_string_utils.h"
#include <chrono>
#include <cmath>
#include <thread>
#include <string>
#include <vector>

using namespace std;
using namespace r_vss;
using r_pipeline::r_encoding;

REGISTER_TEST_FIXTURE(test_r_opus_transcoder);

namespace
{

// G.711 mu-law of a 16-bit sample.
uint8_t _ulaw(int16_t pcm)
{
    const int bias = 0x84, clip = 32635;
    int sign = pcm < 0 ? 0x80 : 0;
    int s = pcm < 0 ? -(int)pcm : pcm;
    if(s > clip)
        s = clip;
    s += bias;
    int exponent = 7;
    for(int mask = 0x4000; (s & mask) == 0 && exponent > 0; mask >>= 1)
        --exponent;
    int mantissa = (s >> (exponent + 3)) & 0x0F;
    return (uint8_t)~(sign | (exponent << 4) | mantissa);
}

// Stored codec params for 8 kHz mono mu-law, as the recorder keeps them.
string _mulaw_params()
{
    string caps = "audio/x-mulaw, rate=(int)8000, channels=(int)1";
    return "encoded_audio_sample_caps=" + r_utils::r_string_utils::to_base64(caps.data(), caps.size());
}

// Pushes seconds of a 1 kHz tone as 20 ms mu-law frames, 20 ms apart as a camera's live
// audio comes, taking packets as it goes, then waits for the rest. The number of Opus
// packets. (Pushed all at once, G.711 comes out short: most of a burst is lost. AAC,
// timed from its decoded samples, isn't. The same as before this moved here.)
size_t _mulaw_tone_packets(r_opus_transcoder& tc, int seconds)
{
    size_t n = 0;
    const int rate = 8000, frame = 160;
    for(int at = 0; at < seconds * rate; at += frame)
    {
        vector<uint8_t> f(frame);
        for(int i = 0; i < frame; ++i)
            f[i] = _ulaw((int16_t)(8000 * sin(2 * 3.14159265358979 * 1000.0 * (at + i) / rate)));
        tc.push(f.data(), f.size(), 1700000000000LL + at / 8);
        this_thread::sleep_for(chrono::milliseconds(20));
        n += tc.take().size();
    }
    for(int i = 0; i < 20; ++i)
    {
        auto more = tc.take(100);
        if(more.empty() && i > 2)
            break;
        n += more.size();
    }
    return n;
}

bool _no_opus(r_encoding enc)
{
    return restream_opus_audio_branch(enc).empty();
}

}

void test_r_opus_transcoder::setup()
{
    r_pipeline::gstreamer_init();
}

void test_r_opus_transcoder::test_mulaw_becomes_opus()
{
    // Where GStreamer can't make Opus there's nothing to run here.
    if(_no_opus(r_encoding::PCMU_ENCODING))
        return;
    r_opus_transcoder tc("PCMU", _mulaw_params());
    RTF_ASSERT(tc.ok());
    RTF_ASSERT(tc.why_not().empty());
    RTF_ASSERT_EQUAL(tc.frame_ms(), (uint32_t)20);
    // 2 s in 20 ms packets: about 100.
    auto n = _mulaw_tone_packets(tc, 2);
    RTF_ASSERT(n >= 90 && n <= 101);
}

void test_r_opus_transcoder::test_packet_lengths()
{
    if(_no_opus(r_encoding::PCMU_ENCODING))
        return;
    // Apple Home may ask for 40 or 60 ms packets; anything else is 20.
    r_opus_transcoder forty("PCMU", _mulaw_params(), 40);
    RTF_ASSERT_EQUAL(forty.frame_ms(), (uint32_t)40);
    auto n = _mulaw_tone_packets(forty, 2);
    RTF_ASSERT(n >= 45 && n <= 51);

    r_opus_transcoder sixty("PCMU", _mulaw_params(), 60);
    RTF_ASSERT_EQUAL(sixty.frame_ms(), (uint32_t)60);
    n = _mulaw_tone_packets(sixty, 3);
    RTF_ASSERT(n >= 45 && n <= 51);

    RTF_ASSERT_EQUAL(r_opus_transcoder("PCMU", _mulaw_params(), 30).frame_ms(), (uint32_t)20);
    RTF_ASSERT_EQUAL(r_opus_transcoder("PCMU", _mulaw_params(), 0).frame_ms(), (uint32_t)20);
}

void test_r_opus_transcoder::test_aac_becomes_opus()
{
    if(_no_opus(r_encoding::AAC_GENERIC_ENCODING))
        return;
    // 2 s of tone as 16 kHz mono AAC, as a camera sends it, with its AudioSpecificConfig
    // in the stored params.
    const int rate = 16000;
    r_av::r_audio_encoder aac(AV_CODEC_ID_AAC, 64000, rate, 1, AV_SAMPLE_FMT_FLTP);
    auto asc = aac.get_extradata();
    r_opus_transcoder tc("mpeg4-generic", "sc_asc=" + r_utils::r_string_utils::to_base64(asc.data(), asc.size()));
    RTF_ASSERT(tc.ok());
    size_t n = 0;
    int fs = aac.get_frame_size();
    for(int at = 0; at + fs <= 2 * rate; at += fs)
    {
        vector<float> f(fs);
        for(int i = 0; i < fs; ++i)
            f[i] = 0.25f * (float)sin(2 * 3.14159265358979 * 1000.0 * (at + i) / rate);
        aac.attach_buffer(reinterpret_cast<const uint8_t*>(f.data()), f.size() * sizeof(float), at);
        if(aac.encode() == r_av::R_CODEC_STATE_HAS_OUTPUT)
        {
            auto p = aac.get();
            tc.push(p.data, p.size, 1700000000000LL + at / 16);
            n += tc.take().size();
        }
    }
    for(int i = 0; i < 20; ++i)
    {
        auto more = tc.take(100);
        if(more.empty() && i > 2)
            break;
        n += more.size();
    }
    // Nearly 100 (less the AAC encoder's start-up delay).
    RTF_ASSERT(n >= 80 && n <= 101);
}

void test_r_opus_transcoder::test_why_not()
{
    // Codecs it doesn't take, and stored params missing what it needs: not ok, with why,
    // and pushing or taking does nothing.
    r_opus_transcoder video("H264", "");
    RTF_ASSERT(!video.ok());
    RTF_ASSERT(!video.why_not().empty());
    uint8_t b[4] = {1, 2, 3, 4};
    video.push(b, sizeof(b), 0);
    RTF_ASSERT(video.take(10).empty());

    r_opus_transcoder unknown("not-a-codec", "");
    RTF_ASSERT(!unknown.ok());
    RTF_ASSERT(unknown.why_not() == "the camera's audio is not-a-codec");

    if(!_no_opus(r_encoding::PCMU_ENCODING))
    {
        r_opus_transcoder no_caps("PCMU", "");
        RTF_ASSERT(!no_caps.ok());
        RTF_ASSERT(no_caps.why_not() == "the camera's audio has no stored caps");
    }
    if(!_no_opus(r_encoding::AAC_GENERIC_ENCODING))
    {
        r_opus_transcoder no_asc("mpeg4-generic", "");
        RTF_ASSERT(!no_asc.ok());
        RTF_ASSERT(no_asc.why_not() == "the camera's AAC has no AudioSpecificConfig");
    }
}

void test_r_opus_transcoder::test_rtp_packet_payload()
{
    auto payload = [](vector<uint8_t> p) { return rtp_packet_payload(p.data(), p.size()); };
    vector<uint8_t> header = {0x80, 111, 0, 1, 0, 0, 0, 2, 0, 0, 0, 3};

    // Header, then payload.
    auto p = header;
    p.insert(p.end(), {9, 8, 7});
    RTF_ASSERT(payload(p) == vector<uint8_t>({9, 8, 7}));

    // Two CSRCs (CC = 2), then an extension of one word, then padding of 2.
    p = header;
    p[0] = 0x80 | 0x20 | 0x10 | 2;
    p.insert(p.end(), {1, 1, 1, 1, 2, 2, 2, 2});
    p.insert(p.end(), {0xBE, 0xDE, 0, 1, 5, 5, 5, 5});
    p.insert(p.end(), {9, 8, 7, 0, 2});
    RTF_ASSERT(payload(p) == vector<uint8_t>({9, 8, 7}));

    // Not RTP 2, too short, no payload, an extension past the end, padding past it, or none.
    p = header;
    p[0] = 0x40;
    p.push_back(1);
    RTF_ASSERT(payload(p).empty());
    RTF_ASSERT(payload(vector<uint8_t>(header.begin(), header.begin() + 11)).empty());
    RTF_ASSERT(payload(header).empty());
    p = header;
    p[0] = 0x90;
    p.insert(p.end(), {0xBE, 0xDE});
    RTF_ASSERT(payload(p).empty());
    p = header;
    p[0] = 0xA0;
    p.insert(p.end(), {1, 2, 40});
    RTF_ASSERT(payload(p).empty());
    p = header;
    p[0] = 0xA0;
    p.insert(p.end(), {1, 2, 0});
    RTF_ASSERT(payload(p).empty());
    RTF_ASSERT(rtp_packet_payload(nullptr, 20).empty());
}
