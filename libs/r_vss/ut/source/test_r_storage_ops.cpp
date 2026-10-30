#include "test_r_storage_ops.h"
#include "r_vss/r_storage_ops.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace r_vss;
using namespace std;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_storage_ops);

// Moving a camera's storage must never lose a recording: the camera is saved
// only once every file is in place, a copied file's original is deleted only
// after that save, and a cancel or failure before it puts everything back.

static fs::path _root;
static fs::path _from;
static fs::path _to;

void test_r_storage_ops::setup()
{
    _root = fs::temp_directory_path() / ("r_storage_ops_ut_" + to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(_root);
    _from = _root / "from";
    _to = _root / "to";
    fs::create_directories(_from);
    fs::create_directories(_to);
}

void test_r_storage_ops::teardown()
{
    std::error_code ec;
    fs::remove_all(_root, ec);
}

static void _write(const fs::path& p, const string& body)
{
    ofstream out(p, ios::binary);
    out << body;
}

static string _read(const fs::path& p)
{
    ifstream in(p, ios::binary);
    return string(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
}

// A recording and its motion file, and where each should go.
static vector<pair<string, string>> _camera_files()
{
    _write(_from / "cam.nts", string(9 * 1024 * 1024, 'n'));  // over two copy chunks
    _write(_from / "cam.mdb", "motion");
    return {
        {(_from / "cam.nts").string(), (_to / "cam.nts").string()},
        {(_from / "cam.mdb").string(), (_to / "cam.mdb").string()},
    };
}

// Another filesystem, as far as move_storage_files can tell: rename fails.
static bool _no_rename(const string&, const string&) { return false; }

void test_r_storage_ops::test_rename_moves_then_commits()
{
    auto moves = _camera_files();
    r_storage_op op;
    bool both_there_at_commit = false;
    move_storage_files(moves, op, [&]() {
        both_there_at_commit = fs::exists(_to / "cam.nts") && fs::exists(_to / "cam.mdb");
    });
    RTF_ASSERT(both_there_at_commit);
    RTF_ASSERT(!fs::exists(_from / "cam.nts") && !fs::exists(_from / "cam.mdb"));
    RTF_ASSERT(_read(_to / "cam.mdb") == "motion");
    RTF_ASSERT(op.progress.load() == 1.f);
}

void test_r_storage_ops::test_copy_keeps_sources_until_commit()
{
    auto moves = _camera_files();
    r_storage_op op;
    bool sources_kept_at_commit = false;
    bool copies_there_at_commit = false;
    move_storage_files(moves, op, [&]() {
        sources_kept_at_commit = fs::exists(_from / "cam.nts") && fs::exists(_from / "cam.mdb");
        copies_there_at_commit = fs::file_size(_to / "cam.nts") == 9 * 1024 * 1024 && _read(_to / "cam.mdb") == "motion";
    }, _no_rename);
    // Until the save, both copies of everything exist; after it, only the new ones.
    RTF_ASSERT(sources_kept_at_commit);
    RTF_ASSERT(copies_there_at_commit);
    RTF_ASSERT(!fs::exists(_from / "cam.nts") && !fs::exists(_from / "cam.mdb"));
    RTF_ASSERT(fs::exists(_to / "cam.nts") && fs::exists(_to / "cam.mdb"));
}

void test_r_storage_ops::test_cancel_partway_puts_everything_back()
{
    // Same filesystem for the recording, then a cancel while the motion file copies:
    // the recording, already renamed, must come back, and nothing is saved.
    for(bool copy_first : {false, true})
    {
        setup();
        auto moves = _camera_files();
        r_storage_op op;
        int renames = 0;
        bool committed = false;
        auto rename_then_cancel = [&](const string& src, const string& dst) -> bool {
            if(renames++ == 0 && !copy_first)
            {
                fs::rename(src, dst);
                return true;
            }
            op.cancel_requested = true;   // the next copy stops at its first chunk
            return false;
        };
        bool threw = false;
        try { move_storage_files(moves, op, [&]() { committed = true; }, rename_then_cancel); }
        catch(const std::exception&) { threw = true; }
        RTF_ASSERT(threw);
        RTF_ASSERT(!committed);
        RTF_ASSERT(fs::file_size(_from / "cam.nts") == 9 * 1024 * 1024);
        RTF_ASSERT(_read(_from / "cam.mdb") == "motion");
        RTF_ASSERT(!fs::exists(_to / "cam.nts") && !fs::exists(_to / "cam.mdb"));
        teardown();
    }
}

void test_r_storage_ops::test_cancel_between_files()
{
    // Cancel pressed once the recording has moved: the motion file, which would rename at
    // once, isn't moved, nothing is saved, and the recording comes back.
    auto moves = _camera_files();
    r_storage_op op;
    bool committed = false;
    auto rename_then_cancel = [&](const string& src, const string& dst) -> bool {
        fs::rename(src, dst);
        op.cancel_requested = true;
        return true;
    };
    bool threw = false;
    try { move_storage_files(moves, op, [&]() { committed = true; }, rename_then_cancel); }
    catch(const std::exception&) { threw = true; }
    RTF_ASSERT(threw);
    RTF_ASSERT(!committed);
    RTF_ASSERT(fs::file_size(_from / "cam.nts") == 9 * 1024 * 1024);
    RTF_ASSERT(_read(_from / "cam.mdb") == "motion");
    RTF_ASSERT(!fs::exists(_to / "cam.nts") && !fs::exists(_to / "cam.mdb"));
}

void test_r_storage_ops::test_failed_save_puts_everything_back()
{
    // Renamed and copied alike: a save that throws leaves the files where the camera says they are.
    for(bool copy : {false, true})
    {
        setup();
        auto moves = _camera_files();
        r_storage_op op;
        bool threw = false;
        try
        {
            move_storage_files(moves, op, []() { throw std::runtime_error("database is locked"); },
                               copy ? std::function<bool(const string&, const string&)>(_no_rename) : nullptr);
        }
        catch(const std::exception& e) { threw = string(e.what()) == "database is locked"; }
        RTF_ASSERT(threw);
        RTF_ASSERT(fs::file_size(_from / "cam.nts") == 9 * 1024 * 1024);
        RTF_ASSERT(_read(_from / "cam.mdb") == "motion");
        RTF_ASSERT(!fs::exists(_to / "cam.nts") && !fs::exists(_to / "cam.mdb"));
        teardown();
    }
}

void test_r_storage_ops::test_failed_copy_leaves_no_partial_file()
{
    // The second file can't be read: the first file's copy is removed, its original kept.
    auto moves = _camera_files();
    moves.push_back({(_from / "missing.mdnts").string(), (_to / "missing.mdnts").string()});
    r_storage_op op;
    bool committed = false;
    bool threw = false;
    try { move_storage_files(moves, op, [&]() { committed = true; }, _no_rename); }
    catch(const std::exception&) { threw = true; }
    RTF_ASSERT(threw);
    RTF_ASSERT(!committed);
    RTF_ASSERT(fs::exists(_from / "cam.nts") && fs::exists(_from / "cam.mdb"));
    RTF_ASSERT(!fs::exists(_to / "cam.nts") && !fs::exists(_to / "cam.mdb") && !fs::exists(_to / "missing.mdnts"));
}
