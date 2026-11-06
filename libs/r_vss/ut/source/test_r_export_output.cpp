#include "test_r_export_output.h"
#include "r_vss/r_export_output.h"
#include "r_storage/r_storage_file.h"
#include "r_utils/r_exception.h"
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace r_vss;
using namespace std;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_export_output);

// An export that ends without its file must say so (its job is then reported failed, not 100% done), and must not
// leave its temp file behind.

static fs::path _dir;

static const string RANGE = "2026-11-05T18:00:00.000 to 2026-11-05T18:05:00.000";

static void _write(const string& path, const string& bytes)
{
    ofstream f(path, ios::binary);
    f << bytes;
}

// The message check() threw, or "" when it didn't throw.
static string _check(const r_export_output& out, bool written, bool stopped)
{
    try
    {
        out.check(written, stopped, RANGE);
    }
    catch(const exception& ex)
    {
        return ex.what();
    }
    return string();
}

void test_r_export_output::setup()
{
    _dir = fs::temp_directory_path() / ("test_r_export_output_" + to_string((long long)chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(_dir);
}

void test_r_export_output::teardown()
{
    error_code ec;
    fs::remove_all(_dir, ec);
}

void test_r_export_output::test_progress_stops_short_of_100()
{
    // 100 is the worker's, once the file is in place: the last chunk queried is still 99.
    RTF_ASSERT(export_percent(0, 300000) == 0);
    RTF_ASSERT(export_percent(150000, 300000) == 50);
    RTF_ASSERT(export_percent(297000, 300000) == 99);
    RTF_ASSERT(export_percent(300000, 300000) == 99);
    RTF_ASSERT(export_percent(400000, 300000) == 99);
    // A range of nothing, or before its start.
    RTF_ASSERT(export_percent(0, 0) == 0);
    RTF_ASSERT(export_percent(-5, 300000) == 0);
}

void test_r_export_output::test_chunk_repeats_left_out()
{
    using r_storage::R_STORAGE_MEDIA_TYPE_VIDEO;
    using r_storage::R_STORAGE_MEDIA_TYPE_AUDIO;
    r_export_output out(_dir.string(), "clip.mp4");

    // The first chunk: video 1000..1200, audio interleaved by its own clock.
    RTF_ASSERT(out.take(R_STORAGE_MEDIA_TYPE_VIDEO, 1000));
    RTF_ASSERT(out.take(R_STORAGE_MEDIA_TYPE_VIDEO, 1100));
    RTF_ASSERT(out.take(R_STORAGE_MEDIA_TYPE_AUDIO, 1050));
    RTF_ASSERT(out.take(R_STORAGE_MEDIA_TYPE_VIDEO, 1200));
    RTF_ASSERT(out.take(R_STORAGE_MEDIA_TYPE_AUDIO, 1150));

    // The next chunk starts back at the key frame at 1100: 1100 and 1200 are repeats, 1300 is new.
    RTF_ASSERT(!out.take(R_STORAGE_MEDIA_TYPE_VIDEO, 1100));
    RTF_ASSERT(!out.take(R_STORAGE_MEDIA_TYPE_VIDEO, 1200));
    RTF_ASSERT(out.take(R_STORAGE_MEDIA_TYPE_VIDEO, 1300));
    // Each stream by its own last frame: audio at 1150 is a repeat, though video is now at 1300.
    RTF_ASSERT(!out.take(R_STORAGE_MEDIA_TYPE_AUDIO, 1150));
    RTF_ASSERT(out.take(R_STORAGE_MEDIA_TYPE_AUDIO, 1250));
    RTF_ASSERT(!out.take(R_STORAGE_MEDIA_TYPE_AUDIO, 1120));
}

void test_r_export_output::test_nothing_written_fails()
{
    r_export_output out(_dir.string(), "clip.mp4");
    RTF_ASSERT(_check(out, false, false).find("Nothing to export in " + RANGE) != string::npos);

    // Every chunk skipped: how many, and the last reason.
    out.skipped("Blob tree missing frames array.");
    out.skipped("Timestamp is not monotonically increasing.");
    RTF_ASSERT(out.skipped_chunks() == 2);
    auto msg = _check(out, false, false);
    RTF_ASSERT(msg.find("2 chunk(s) skipped") != string::npos);
    RTF_ASSERT(msg.find("the last: Timestamp is not monotonically increasing.") != string::npos);

    // Something written, even with chunks skipped: it can finish.
    RTF_ASSERT(_check(out, true, false).empty());

    // An r_exception's what() goes on with its stack: the error is its first line.
    try
    {
        R_THROW(("No frames here."));
    }
    catch(const exception& ex)
    {
        RTF_ASSERT(string(ex.what()).find('\n') != string::npos);
        RTF_ASSERT(export_message(ex) == "No frames here.");
    }
    RTF_ASSERT(export_message(runtime_error("plain")) == "plain");
}

void test_r_export_output::test_stopped_fails()
{
    r_export_output out(_dir.string(), "clip.mp4");
    RTF_ASSERT(_check(out, true, true).find("stopped") != string::npos);
    RTF_ASSERT(_check(out, false, true).find("stopped") != string::npos);
}

void test_r_export_output::test_written_is_renamed_into_place()
{
    string temp, output;
    {
        r_export_output out(_dir.string(), "clip.mp4");
        temp = out.temp_path();
        output = out.output_path();
        RTF_ASSERT(output == (_dir / "clip.mp4").string());
        RTF_ASSERT(temp != output && temp.size() > 4 && temp.substr(temp.size() - 4) == ".mp4");
        RTF_ASSERT(fs::path(temp).parent_path() == _dir);
        _write(temp, "moov");  // the finalized file
        out.commit();
    }
    // Kept once committed, after the r_export_output is gone.
    RTF_ASSERT(fs::exists(output) && !fs::exists(temp));
    RTF_ASSERT(fs::file_size(output) == 4);
}

void test_r_export_output::test_unfinished_leaves_no_temp_file()
{
    string temp;
    {
        r_export_output out(_dir.string(), "clip.mp4");
        temp = out.temp_path();
        _write(temp, "partial");
        out.skipped("decode failed");
        RTF_ASSERT(!_check(out, false, false).empty());
    }
    RTF_ASSERT(!fs::exists(temp));
    RTF_ASSERT(!fs::exists(_dir / "clip.mp4"));

    // Nothing ever created: nothing to remove, and no throw from the destructor.
    {
        r_export_output out(_dir.string(), "clip.mp4");
    }
    RTF_ASSERT(fs::is_empty(_dir));
}
