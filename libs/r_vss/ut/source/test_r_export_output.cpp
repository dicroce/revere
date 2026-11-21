#include "test_r_export_output.h"
#include "r_vss/r_export_output.h"
#include "r_av/r_muxer.h"
#include "r_av/r_demuxer.h"
#include "r_utils/r_file.h"
#include <filesystem>
#include <string>
#include <vector>

using namespace r_vss;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_r_export_output);

static const char* const DIR = "export_output_dir";

void test_r_export_output::setup()
{
    teardown();
    std::filesystem::create_directories(DIR);
}

void test_r_export_output::teardown()
{
    std::filesystem::remove_all(DIR);
}

static size_t _files_in(const string& dir)
{
    size_t n = 0;
    for(auto& e : std::filesystem::directory_iterator(dir))
    {
        (void)e;
        ++n;
    }
    return n;
}

// Writes a second of mu-law audio (8 kHz mono, 20 ms packets) to an opened muxer.
static void _write_mulaw(r_av::r_muxer& m)
{
    vector<uint8_t> packet(160, 0xFF);
    for(int64_t ms = 0; ms < 1000; ms += 20)
        m.write_audio_frame(packet.data(), packet.size(), ms, {1, 1000});
}

void test_r_export_output::test_header_failure_fails_and_cleans_up()
{
    // A mu-law camera exported to .mp4: MP4 can't hold pcm_mulaw, so every chunk's header write fails. The tmp_ file
    // the muxer opened is there (empty) until the export gives up; then finish() throws, so the job reports failed,
    // and nothing is left in the exports folder.
    string temp;
    bool threw = false;
    string what;
    {
        r_export_output out(DIR, "clip.mp4");
        temp = out.temp_path();
        RTF_ASSERT(temp.size() > 4 && temp.substr(temp.size() - 4) == ".mp4");
        RTF_ASSERT(temp.find("tmp_") != string::npos);
        RTF_ASSERT_EQUAL(out.output_path(), string(DIR) + PATH_SLASH + "clip.mp4");
        {
            r_av::r_muxer muxer(out.temp_path());
            try
            {
                muxer.add_audio_stream(AV_CODEC_ID_PCM_MULAW, 1, 8000);
                muxer.open();
                out.opened();
            }
            catch(const exception& ex)
            {
                out.skipped(ex.what());
            }
            out.skipped("a later chunk's reason");   // the first is the one kept
            RTF_ASSERT(!out.is_open());
            RTF_ASSERT(r_fs::file_exists(temp));
            try
            {
                out.finish(muxer);
            }
            catch(const exception& ex)
            {
                threw = true;
                what = ex.what();
            }
        }
    }
    RTF_ASSERT(threw);
    RTF_ASSERT(what.find("Export wrote nothing: ") != string::npos);
    RTF_ASSERT(what.find("Unable to write header") != string::npos);
    RTF_ASSERT(what.find("a later chunk") == string::npos);
    RTF_ASSERT(!r_fs::file_exists(temp));
    RTF_ASSERT(!r_fs::file_exists(string(DIR) + PATH_SLASH + "clip.mp4"));
    RTF_ASSERT_EQUAL(_files_in(DIR), (size_t)0);

    // Nothing skipped and nothing written (the export stopped first) still fails.
    {
        r_export_output out(DIR, "none.mp4");
        r_av::r_muxer muxer(out.temp_path());
        threw = false;
        try { out.finish(muxer); } catch(const exception& ex) { threw = true; what = ex.what(); }
        RTF_ASSERT(threw);
        RTF_ASSERT(what.find("Export wrote nothing: no chunk was written") != string::npos);
    }
    RTF_ASSERT_EQUAL(_files_in(DIR), (size_t)0);
}

void test_r_export_output::test_finished_export_is_renamed()
{
    // A container that holds the audio (.mov holds mu-law): finish() renames the tmp_ file to the name asked for.
    // With faststart, as the exports use: writing the trailer reopens the file by its tmp_ name, so finish() has
    // to finalize before it renames.
    string temp, output;
    {
        r_export_output out(DIR, "clip.mov");
        temp = out.temp_path();
        output = out.output_path();
        r_av::r_muxer muxer(out.temp_path());
        muxer.enable_faststart();
        muxer.add_audio_stream(AV_CODEC_ID_PCM_MULAW, 1, 8000);
        muxer.open();
        out.opened();
        _write_mulaw(muxer);
        out.finish(muxer);
    }
    RTF_ASSERT(!r_fs::file_exists(temp));
    RTF_ASSERT(r_fs::file_exists(output));
    RTF_ASSERT(std::filesystem::file_size(output) > 8000);
    RTF_ASSERT_EQUAL(_files_in(DIR), (size_t)1);

    // And it was finalized: it plays (reads back) with the second of audio written.
    int packets = 0;
    {
        r_av::r_demuxer d(output, false);
        auto ai = d.get_audio_stream_index();
        RTF_ASSERT(ai >= 0);
        RTF_ASSERT_EQUAL(d.get_stream_info(ai).codec_id, AV_CODEC_ID_PCM_MULAW);
        while(d.read_frame())
            if(d.get_frame_info().index == ai)
                ++packets;
    }
    RTF_ASSERT(packets >= 1);
}

void test_r_export_output::test_unfinished_export_is_removed()
{
    // Opened and written, but the export threw before finish() (a transcode that mostly didn't decode): the tmp_
    // file is removed and there's no output.
    string temp, output;
    {
        r_export_output out(DIR, "clip.mov");
        temp = out.temp_path();
        output = out.output_path();
        r_av::r_muxer muxer(out.temp_path());
        muxer.add_audio_stream(AV_CODEC_ID_PCM_MULAW, 1, 8000);
        muxer.open();
        out.opened();
        _write_mulaw(muxer);
        RTF_ASSERT(r_fs::file_exists(temp));
    }
    RTF_ASSERT(!r_fs::file_exists(temp));
    RTF_ASSERT(!r_fs::file_exists(output));
    RTF_ASSERT_EQUAL(_files_in(DIR), (size_t)0);
}
