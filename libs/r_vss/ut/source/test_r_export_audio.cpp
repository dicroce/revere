#include "test_r_export_audio.h"
#include "r_vss/r_export_audio.h"
#include "r_av/r_muxer.h"
#include "r_av/r_demuxer.h"
#include "r_utils/r_file.h"
#include <filesystem>
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_export_audio);

static const char* const DIR = "export_audio_dir";

void test_r_export_audio::setup()
{
    teardown();
    std::filesystem::create_directories(DIR);
}

void test_r_export_audio::teardown()
{
    std::filesystem::remove_all(DIR);
}

static string _path(const string& name)
{
    return string(DIR) + PATH_SLASH + name;
}

// Stored G.711 packets as a camera sends them: 8 kHz mono, 20 ms (160 bytes) each, from from_ms up to to_ms.
static void _write_g711(r_export_audio& a, r_av::r_muxer& m, int64_t from_ms, int64_t to_ms)
{
    vector<uint8_t> packet(160);
    for(int64_t ms = from_ms; ms < to_ms; ms += 20)
    {
        for(size_t i = 0; i < packet.size(); ++i)
            packet[i] = (uint8_t)((ms + (int64_t)i * 37) & 0xFF);
        a.write(m, packet.data(), packet.size(), ms);
    }
}

struct audio_read
{
    r_av::r_stream_info info {};
    size_t packets {0};
    vector<int64_t> pts_ms;
};

static audio_read _read_audio(const string& path)
{
    audio_read r;
    r_av::r_demuxer d(path, false);
    auto ai = d.get_audio_stream_index();
    RTF_ASSERT(ai >= 0);
    r.info = d.get_stream_info(ai);
    while(d.read_frame())
    {
        auto fi = d.get_frame_info();
        if(fi.index != ai)
            continue;
        ++r.packets;
        r.pts_ms.push_back(av_rescale_q(fi.pts, r.info.time_base, {1, 1000}));
    }
    return r;
}

// Muxes [from, to) of G.711 into file_name and returns what was read back.
static audio_read _export(const string& codec, const string& file_name, int64_t from_ms, int64_t to_ms)
{
    auto path = _path(file_name);
    {
        r_export_audio a(codec, "sc_audio_rate=8000, sc_audio_channels=1", file_name);
        r_av::r_muxer m(path);
        a.add_stream(m);
        m.open();
        _write_g711(a, m, from_ms, to_ms);
        a.flush(m);
        m.finalize();
    }
    return _read_audio(path);
}

void test_r_export_audio::test_container_holds()
{
    RTF_ASSERT_EQUAL(export_container_holds("clip.mp4", AV_CODEC_ID_PCM_MULAW), 0);
    RTF_ASSERT_EQUAL(export_container_holds("clip.mp4", AV_CODEC_ID_PCM_ALAW), 0);
    RTF_ASSERT_EQUAL(export_container_holds("clip.mp4", AV_CODEC_ID_AAC), 1);
    RTF_ASSERT_EQUAL(export_container_holds("clip.mov", AV_CODEC_ID_PCM_MULAW), 1);
    RTF_ASSERT_EQUAL(export_container_holds("clip.mkv", AV_CODEC_ID_AAC), 1);
    RTF_ASSERT_EQUAL(export_container_holds("clip.not-a-type", AV_CODEC_ID_AAC), -1);
}

void test_r_export_audio::test_g711_in_mp4_becomes_aac()
{
    // MP4 can't hold G.711 (its header write fails: "Could not find tag for codec pcm_mulaw"), so it's encoded as
    // AAC at the camera's rate: 2 s of audio in, about 2 s of AAC out, starting at the packets' own time.
    for(auto codec : {"PCMU", "PCMA"})
    {
        RTF_ASSERT(r_export_audio(codec, "sc_audio_rate=8000", "clip.mp4").what() == r_export_audio::mode::aac);
        auto r = _export(codec, string(codec) + ".mp4", 0, 2000);
        RTF_ASSERT_EQUAL(r.info.codec_id, AV_CODEC_ID_AAC);
        RTF_ASSERT_EQUAL(r.info.sample_rate, (uint32_t)8000);
        RTF_ASSERT_EQUAL(r.info.channels, (uint8_t)1);
        // 16000 samples is 15.6 frames of 1024 (128 ms each): 16 frames, the last padded with silence, and the
        // encoder may add one for its delay.
        RTF_ASSERT(r.packets >= 16 && r.packets <= 17);
        // Each packet's time is its start, 128 ms (the encoder's delay) ahead of the audio it decodes to: the first
        // is at -128 ms (the priming the file's edit list trims), and the last ends about 2 s in, less that delay.
        RTF_ASSERT(r.pts_ms.front() <= 0 && r.pts_ms.front() >= -128);
        RTF_ASSERT(r.pts_ms.back() + 128 >= 1850 && r.pts_ms.back() + 128 <= 2200);
        for(size_t i = 1; i < r.pts_ms.size(); ++i)
            RTF_ASSERT(r.pts_ms[i] > r.pts_ms[i - 1]);
    }

    // No rate stored: G.711 is 8 kHz.
    RTF_ASSERT(r_export_audio("PCMU", "", "clip.mp4").what() == r_export_audio::mode::aac);
}

void test_r_export_audio::test_copied_where_the_container_holds_it()
{
    // .mov holds G.711: the packets go in as they are (as before), 50 a second.
    RTF_ASSERT(r_export_audio("PCMU", "sc_audio_rate=8000", "clip.mov").what() == r_export_audio::mode::copy);
    auto r = _export("PCMU", "clip.mov", 0, 2000);
    RTF_ASSERT_EQUAL(r.info.codec_id, AV_CODEC_ID_PCM_MULAW);
    RTF_ASSERT(r.packets >= 1);

    // AAC from the camera into .mp4: copied.
    RTF_ASSERT(r_export_audio("mpeg4-generic", "sc_audio_rate=16000", "clip.mp4").what() == r_export_audio::mode::copy);

    // A file type FFmpeg can't name (export_container_holds says -1, not known): copied, as before this, rather than
    // re-encoded or dropped on a guess.
    RTF_ASSERT(r_export_audio("PCMU", "sc_audio_rate=8000", "clip.not-a-type").what() == r_export_audio::mode::copy);
}

void test_r_export_audio::test_dropped_with_why()
{
    // Audio the export can't carry leaves the video exported without it, instead of failing the whole export.
    r_export_audio webm("PCMU", "sc_audio_rate=8000", "clip.webm");
    RTF_ASSERT(webm.what() == r_export_audio::mode::drop);
    RTF_ASSERT_EQUAL(webm.why_dropped(), string("the file type can't hold PCMU audio"));

    r_export_audio unknown("G726-32", "sc_audio_rate=8000", "clip.mp4");
    RTF_ASSERT(unknown.what() == r_export_audio::mode::drop);
    RTF_ASSERT_EQUAL(unknown.why_dropped(), string("its codec (G726-32) isn't one the export knows"));

    r_export_audio no_rate("mpeg4-generic", "sc_audio_channels=2", "clip.mp4");
    RTF_ASSERT(no_rate.what() == r_export_audio::mode::drop);
    RTF_ASSERT_EQUAL(no_rate.why_dropped(), string("its sample rate isn't stored"));

    // A dropped stream adds nothing and writes nothing.
    r_av::r_muxer m(_path("dropped.mp4"));
    webm.add_stream(m);
    uint8_t b[160] = {};
    webm.write(m, b, sizeof(b), 0);
    webm.flush(m);
    RTF_ASSERT(!r_fs::file_exists(_path("dropped.mp4")));
}

void test_r_export_audio::test_audio_after_a_gap_keeps_its_time()
{
    // A second of audio, a 2 s outage, then a second more: the audio after it starts at 3 s, in step with the video,
    // not straight after the first second.
    auto path = _path("gap.mp4");
    {
        r_export_audio a("PCMU", "sc_audio_rate=8000", "gap.mp4");
        r_av::r_muxer m(path);
        a.add_stream(m);
        m.open();
        _write_g711(a, m, 0, 1000);
        _write_g711(a, m, 3000, 4000);
        a.flush(m);
        m.finalize();
    }
    auto r = _read_audio(path);
    // The first second ends by about 1.1 s (its last frame padded), nothing starts in the gap, and the audio after
    // it starts at 3 s (less a new encoder's 128 ms priming, as at the start) and ends about 4 s in.
    int64_t after = -1;
    for(auto ms : r.pts_ms)
    {
        RTF_ASSERT(ms < 1100 || ms >= 2850);
        if(after < 0 && ms >= 2850)
            after = ms;
    }
    RTF_ASSERT(after >= 2862 && after <= 2882);   // 3000 - 128, like the -128 at the start
    RTF_ASSERT(r.pts_ms.back() + 128 >= 3850 && r.pts_ms.back() + 128 <= 4200);
    for(size_t i = 1; i < r.pts_ms.size(); ++i)
        RTF_ASSERT(r.pts_ms[i] > r.pts_ms[i - 1]);
}
