#include "test_r_plugin_loader.h"
#include "r_vss/r_system_plugin_host.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

using namespace r_vss;
using namespace std;
namespace fs = std::filesystem;

REGISTER_TEST_FIXTURE(test_r_plugin_loader);

namespace
{

string _read(const string& path)
{
    ifstream f(path);
    stringstream s;
    s << f.rdbuf();
    return s.str();
}

void _write(const string& path, const string& contents)
{
    ofstream f(path, ios::binary);
    f << contents;
}

r_plugin_check_result _check(r_plugin_check status)
{
    r_plugin_check_result r;
    r.status = status;
    return r;
}

string _name(const char* plugin)
{
    return fs::path(plugin).stem().string();
}

string _file(const char* plugin)
{
    return fs::path(plugin).filename().string();
}

}

void test_r_plugin_loader::setup()
{
    // A top_dir of its own, with an exe-like folder and a user folder, both empty.
    _top = (fs::temp_directory_path() / ("r_vss_plugin_loader_" + to_string(random_device()()))).string();
    fs::create_directories(fs::path(_top) / "exe" / "system_plugins");
    fs::create_directories(fs::path(_top) / "system_plugins");
}

void test_r_plugin_loader::teardown()
{
    error_code ec;
    fs::remove_all(_top, ec);
}

void test_r_plugin_loader::test_choose_copy()
{
    using c = r_plugin_check;
    // The last copy that passed: a user folder's, scanned after the exe's, wins.
    RTF_ASSERT_EQUAL(choose_system_plugin_copy({_check(c::ok), _check(c::ok)}, true), 1);
    RTF_ASSERT_EQUAL(choose_system_plugin_copy({_check(c::ok), _check(c::ok)}, false), 1);
    RTF_ASSERT_EQUAL(choose_system_plugin_copy({_check(c::ok), _check(c::hash_mismatch)}, true), 0);
    RTF_ASSERT_EQUAL(choose_system_plugin_copy({_check(c::no_manifest), _check(c::ok)}, true), 1);
    // None passed: the first, as before, unless signatures are required.
    RTF_ASSERT_EQUAL(choose_system_plugin_copy({_check(c::no_manifest), _check(c::bad_signature)}, false), 0);
    RTF_ASSERT_EQUAL(choose_system_plugin_copy({_check(c::no_manifest), _check(c::bad_signature)}, true), -1);
    RTF_ASSERT_EQUAL(choose_system_plugin_copy({_check(c::wrong_version)}, true), -1);
    RTF_ASSERT_EQUAL(choose_system_plugin_copy({}, false), -1);
}

void test_r_plugin_loader::test_needs_update()
{
    r_skipped_system_plugin s;
    for(auto status : {r_plugin_check::no_manifest, r_plugin_check::wrong_version})
    {
        s.check = _check(status);
        RTF_ASSERT(s.needs_update());
    }
    for(auto status : {r_plugin_check::bad_signature, r_plugin_check::malformed, r_plugin_check::wrong_file,
                       r_plugin_check::wrong_platform, r_plugin_check::size_mismatch, r_plugin_check::hash_mismatch,
                       r_plugin_check::unreadable, r_plugin_check::ok})
    {
        s.check = _check(status);
        RTF_ASSERT(!s.needs_update());
    }
}

void test_r_plugin_loader::test_unsigned_build_loads_the_first_copy()
{
    // The same plugin in both folders, unsigned: loaded once, as before.
    auto exe_dir = (fs::path(_top) / "exe" / "system_plugins").string();
    auto user_dir = (fs::path(_top) / "system_plugins").string();
    fs::copy_file(TEST_ACTION_PLUGIN, fs::path(exe_dir) / _file(TEST_ACTION_PLUGIN));
    fs::copy_file(TEST_ACTION_PLUGIN, fs::path(user_dir) / _file(TEST_ACTION_PLUGIN));

    r_system_plugin_dirs dirs;
    dirs.load = {exe_dir, user_dir};
    r_system_plugin_host host(_top, dirs, false);
    auto loaded = host.get_loaded_plugins();
    RTF_ASSERT_EQUAL(loaded.size(), (size_t)1);
    RTF_ASSERT(loaded[0] == _name(TEST_ACTION_PLUGIN));
    RTF_ASSERT(host.get_skipped_plugins().empty());
}

void test_r_plugin_loader::test_required_skips_unsigned_plugins()
{
    // Requiring signatures: a plugin with no manifest needs updating, and one whose
    // manifest isn't signed by a trusted key failed its check. Neither is opened.
    auto exe_dir = (fs::path(_top) / "exe" / "system_plugins").string();
    auto user_dir = (fs::path(_top) / "system_plugins").string();
    fs::copy_file(TEST_ACTION_PLUGIN, fs::path(exe_dir) / _file(TEST_ACTION_PLUGIN));
    fs::copy_file(TEST_ACTION_PLUGIN, fs::path(user_dir) / _file(TEST_ACTION_PLUGIN));
    auto no_action = (fs::path(user_dir) / _file(TEST_NO_ACTION_PLUGIN)).string();
    fs::copy_file(TEST_NO_ACTION_PLUGIN, no_action);
    _write(plugin_manifest_path(no_action), "{\"format\": 1}");
    _write(plugin_signature_path(no_action), "not a signature");

    r_system_plugin_dirs dirs;
    dirs.load = {exe_dir, user_dir};
    r_system_plugin_host host(_top, dirs, true);
    RTF_ASSERT(host.get_loaded_plugins().empty());

    auto skipped = host.get_skipped_plugins();
    RTF_ASSERT_EQUAL(skipped.size(), (size_t)2);
    for(const auto& s : skipped)
    {
        if(s.name == _name(TEST_ACTION_PLUGIN))
        {
            RTF_ASSERT(s.check.status == r_plugin_check::no_manifest);
            RTF_ASSERT(s.needs_update());
            // The user folder's copy is the one reported: it's the one an install replaces.
            RTF_ASSERT(fs::path(s.path).parent_path() == fs::path(user_dir));
        }
        else
        {
            RTF_ASSERT(s.name == _name(TEST_NO_ACTION_PLUGIN));
            RTF_ASSERT(s.check.status == r_plugin_check::bad_signature);
            RTF_ASSERT(!s.needs_update());
        }
    }
}

void test_r_plugin_loader::test_load_and_start()
{
    // Nothing found at start.
    r_system_plugin_dirs dirs;
    dirs.load = {(fs::path(_top) / "system_plugins").string()};
    r_system_plugin_host host(_top, dirs, false);
    RTF_ASSERT(host.get_loaded_plugins().empty());

    // Loaded before start_all(): not started until it runs.
    string why;
    RTF_ASSERT(host.load_and_start(TEST_NO_ACTION_PLUGIN, why));
    RTF_ASSERT(!fs::exists(_top + "/lifecycle.txt"));
    host.start_all();
    RTF_ASSERT(_read(_top + "/lifecycle.txt") == "start\n");

    // Loaded after: started at once, and working.
    RTF_ASSERT(host.load_and_start(TEST_ACTION_PLUGIN, why));
    RTF_ASSERT(_read(_top + "/lifecycle.txt") == "start\nstart\n");
    auto loaded = host.get_loaded_plugins();
    RTF_ASSERT_EQUAL(loaded.size(), (size_t)2);
    RTF_ASSERT(loaded[1] == _name(TEST_ACTION_PLUGIN));
    RTF_ASSERT(host.run_plugin_action(_name(TEST_ACTION_PLUGIN), "hello"));
    RTF_ASSERT(_read(_top + "/actions.txt") == "hello\n");

    // Not twice, and not a file that isn't a plugin.
    RTF_ASSERT(!host.load_and_start(TEST_ACTION_PLUGIN, why));
    RTF_ASSERT(why.find("already loaded") != string::npos);
    auto not_a_plugin = _top + "/notes.txt";
    _write(not_a_plugin, "hello");
    RTF_ASSERT(!host.load_and_start(not_a_plugin, why));
    RTF_ASSERT_EQUAL(host.get_loaded_plugins().size(), (size_t)2);

    host.stop_all();
    RTF_ASSERT(_read(_top + "/lifecycle.txt") == "start\nstart\nstop\nstop\n");
}

void test_r_plugin_loader::test_load_and_start_checks()
{
    // Requiring signatures, an unsigned plugin isn't loaded; get_skipped_plugins says why.
    r_system_plugin_dirs dirs;
    dirs.load = {(fs::path(_top) / "system_plugins").string()};
    r_system_plugin_host host(_top, dirs, true);
    host.start_all();

    string why;
    RTF_ASSERT(!host.load_and_start(TEST_ACTION_PLUGIN, why));
    RTF_ASSERT(why.find("no_manifest") != string::npos);
    RTF_ASSERT(host.get_loaded_plugins().empty());
    RTF_ASSERT(!fs::exists(_top + "/lifecycle.txt"));

    auto skipped = host.get_skipped_plugins();
    RTF_ASSERT_EQUAL(skipped.size(), (size_t)1);
    RTF_ASSERT(skipped[0].name == _name(TEST_ACTION_PLUGIN));
    RTF_ASSERT(skipped[0].needs_update());

    // A second try replaces the entry rather than adding one.
    RTF_ASSERT(!host.load_and_start(TEST_ACTION_PLUGIN, why));
    RTF_ASSERT_EQUAL(host.get_skipped_plugins().size(), (size_t)1);
}

void test_r_plugin_loader::test_uninstall()
{
    // One plugin next to the exe, one in the user folder: only the user's can be
    // uninstalled. Marking it changes nothing now; at the next start it's deleted,
    // with its manifest and signature, and not loaded.
    auto exe_dir = (fs::path(_top) / "exe" / "system_plugins").string();
    auto user_dir = (fs::path(_top) / "system_plugins").string();
    fs::copy_file(TEST_NO_ACTION_PLUGIN, fs::path(exe_dir) / _file(TEST_NO_ACTION_PLUGIN));
    auto user_copy = (fs::path(user_dir) / _file(TEST_ACTION_PLUGIN)).string();
    fs::copy_file(TEST_ACTION_PLUGIN, user_copy);
    _write(plugin_manifest_path(user_copy), "{\"format\": 1}");
    _write(plugin_signature_path(user_copy), "not a signature");

    r_system_plugin_dirs dirs;
    dirs.load = {exe_dir, user_dir};
    {
        r_system_plugin_host host(_top, dirs, false);
        RTF_ASSERT_EQUAL(host.get_loaded_plugins().size(), (size_t)2);
        RTF_ASSERT(host.can_uninstall(_name(TEST_ACTION_PLUGIN)));
        RTF_ASSERT(!host.can_uninstall(_name(TEST_NO_ACTION_PLUGIN)));
        RTF_ASSERT(!host.can_uninstall("not_a_plugin"));

        string why;
        RTF_ASSERT(!host.mark_for_uninstall(_name(TEST_NO_ACTION_PLUGIN), why));
        RTF_ASSERT(!why.empty());
        RTF_ASSERT(!fs::exists(plugin_uninstall_marker_path((fs::path(exe_dir) / _file(TEST_NO_ACTION_PLUGIN)).string())));

        RTF_ASSERT(host.mark_for_uninstall(_name(TEST_ACTION_PLUGIN), why));
        RTF_ASSERT(fs::exists(plugin_uninstall_marker_path(user_copy)));
        RTF_ASSERT_EQUAL(host.get_loaded_plugins().size(), (size_t)2);
    }

    r_system_plugin_host host(_top, dirs, false);
    auto loaded = host.get_loaded_plugins();
    RTF_ASSERT_EQUAL(loaded.size(), (size_t)1);
    RTF_ASSERT(loaded[0] == _name(TEST_NO_ACTION_PLUGIN));
    for(const auto& path : {user_copy, plugin_manifest_path(user_copy), plugin_signature_path(user_copy), plugin_uninstall_marker_path(user_copy)})
        RTF_ASSERT(!fs::exists(path));
    RTF_ASSERT(fs::exists(fs::path(exe_dir) / _file(TEST_NO_ACTION_PLUGIN)));
}

void test_r_plugin_loader::test_uninstall_skipped()
{
    // A copy in the user folder that isn't loaded (it needs updating) can be uninstalled too.
    auto user_dir = (fs::path(_top) / "system_plugins").string();
    auto user_copy = (fs::path(user_dir) / _file(TEST_ACTION_PLUGIN)).string();
    fs::copy_file(TEST_ACTION_PLUGIN, user_copy);

    r_system_plugin_dirs dirs;
    dirs.load = {(fs::path(_top) / "exe" / "system_plugins").string(), user_dir};
    {
        r_system_plugin_host host(_top, dirs, true);
        RTF_ASSERT(host.get_loaded_plugins().empty());
        RTF_ASSERT(host.can_uninstall(_name(TEST_ACTION_PLUGIN)));
        string why;
        RTF_ASSERT(host.mark_for_uninstall(_name(TEST_ACTION_PLUGIN), why));
    }
    r_system_plugin_host host(_top, dirs, true);
    RTF_ASSERT(host.get_skipped_plugins().empty());
    RTF_ASSERT(!fs::exists(user_copy));
}

void test_r_plugin_loader::test_uninstall_when_delete_fails()
{
    // The plugin can't be deleted: it still isn't loaded, and the mark stays for the next start.
    auto user_dir = (fs::path(_top) / "system_plugins").string();
    auto user_copy = (fs::path(user_dir) / _file(TEST_ACTION_PLUGIN)).string();
    fs::copy_file(TEST_ACTION_PLUGIN, user_copy);
    _write(plugin_uninstall_marker_path(user_copy), "uninstall\n");

    r_system_plugin_dirs dirs;
    dirs.load = {(fs::path(_top) / "exe" / "system_plugins").string(), user_dir};
#ifdef IS_WINDOWS
    {
        // An open file can't be deleted on Windows.
        ifstream held(user_copy, ios::binary);
        RTF_ASSERT(held.is_open());
        r_system_plugin_host host(_top, dirs, false);
        RTF_ASSERT(host.get_loaded_plugins().empty());
        RTF_ASSERT(fs::exists(user_copy));
        RTF_ASSERT(fs::exists(plugin_uninstall_marker_path(user_copy)));
    }
#endif
    // Once it can be, it is.
    r_system_plugin_host host(_top, dirs, false);
    RTF_ASSERT(host.get_loaded_plugins().empty());
    RTF_ASSERT(!fs::exists(user_copy));
    RTF_ASSERT(!fs::exists(plugin_uninstall_marker_path(user_copy)));
}
