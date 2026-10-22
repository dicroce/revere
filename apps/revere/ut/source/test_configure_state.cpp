#include "test_configure_state.h"
#include "configure_state.h"
#include "r_utils/r_file.h"
#include "r_utils/r_string_utils.h"
#include <filesystem>
#include <algorithm>
#include <vector>

using namespace revere;
using namespace r_utils;
using namespace std;

REGISTER_TEST_FIXTURE(test_configure_state);

static string _cfg_path(const string& top_dir)
{
    return top_dir + PATH_SLASH + "config" + PATH_SLASH + "revere_cfg.json";
}

static void _write(const string& path, const string& text)
{
    r_fs::write_file((const uint8_t*)text.data(), text.size(), path);
}

static string _read(const string& path)
{
    auto fb = r_fs::read_file(path);
    return string((char*)fb.data(), fb.size());
}

static vector<string> _config_files(const string& top_dir)
{
    vector<string> names;
    for(auto& e : std::filesystem::directory_iterator(top_dir + PATH_SLASH + "config"))
        names.push_back(e.path().filename().string());
    sort(names.begin(), names.end());
    return names;
}

void test_configure_state::setup()
{
    _top_dir = r_fs::temp_file_name(".", "cfgut_");
    r_fs::mkdir_p(_top_dir + PATH_SLASH + "config");
}

void test_configure_state::teardown()
{
    std::filesystem::remove_all(_top_dir);
}

void test_configure_state::test_no_file()
{
    configure_state cs(_top_dir);
    cs.load();
    RTF_ASSERT(cs.load_problem().empty());
    RTF_ASSERT(!cs.has_camera_answers("cam1"));
    RTF_ASSERT(!cs.get_window_placement());
}

void test_configure_state::test_round_trip()
{
    {
        configure_state cs(_top_dir);
        cs.load();
        cs.set_camera_answer("cam1", 7, "Private Space");
        window_placement wp;
        wp.rect = {10, 20, 800, 600};
        cs.set_window_placement(wp);
        cs.set_section_open("discovered", false);
        cs.save();
    }
    configure_state cs(_top_dir);
    cs.load();
    RTF_ASSERT(cs.load_problem().empty());
    RTF_ASSERT(cs.get_camera_answer("cam1", 7) == string("private space"));
    auto wp = cs.get_window_placement();
    RTF_ASSERT(wp && wp->rect == (r_ui_utils::window_rect{10, 20, 800, 600}));
    RTF_ASSERT(!cs.get_section_open("discovered", true));
    RTF_ASSERT(cs.get_section_open("recording", true));
}

void test_configure_state::test_truncated_file_kept()
{
    // What a crash mid-save used to leave: the start of the file only.
    string cut = "{\"camera_answers\":{\"cam1\":{\"7\":\"Priv";
    _write(_cfg_path(_top_dir), cut);

    configure_state cs(_top_dir);
    cs.load();  // doesn't throw
    RTF_ASSERT(!cs.load_problem().empty());
    RTF_ASSERT(!cs.has_camera_answers("cam1"));

    // The bad file was moved aside, intact, and is named in the message.
    auto files = _config_files(_top_dir);
    RTF_ASSERT(files.size() == 1);
    RTF_ASSERT(r_string_utils::starts_with(files[0], "revere_cfg.json.bad-"));
    RTF_ASSERT(r_string_utils::contains(cs.load_problem(), files[0]));
    auto bad_path = _top_dir + PATH_SLASH + "config" + PATH_SLASH + files[0];
    RTF_ASSERT(_read(bad_path) == cut);

    // Saving now writes a fresh file next to it, without touching the bad copy.
    cs.set_camera_answer("cam2", 1, "Yes");
    cs.save();
    RTF_ASSERT(r_fs::file_exists(_cfg_path(_top_dir)));
    RTF_ASSERT(_read(bad_path) == cut);

    configure_state again(_top_dir);
    again.load();
    RTF_ASSERT(again.load_problem().empty());
    RTF_ASSERT(again.get_camera_answer("cam2", 1) == string("yes"));
}

void test_configure_state::test_bad_answers_kept()
{
    // Valid JSON, but an answer that isn't a string: also not loaded by halves.
    string odd = "{\"camera_answers\":{\"cam1\":{\"7\":\"Yes\",\"8\":42}},\"window\":{\"x\":1,\"y\":2,\"w\":300,\"h\":200},\"sections\":{\"plugins\":false}}";
    _write(_cfg_path(_top_dir), odd);

    configure_state cs(_top_dir);
    cs.load();
    RTF_ASSERT(!cs.load_problem().empty());
    RTF_ASSERT(!cs.has_camera_answers("cam1"));
    RTF_ASSERT(!cs.get_window_placement());
    RTF_ASSERT(cs.get_section_open("plugins", true));
    RTF_ASSERT(!r_fs::file_exists(_cfg_path(_top_dir)));
    RTF_ASSERT(_config_files(_top_dir).size() == 1);
}

void test_configure_state::test_save_leaves_no_temp()
{
    configure_state cs(_top_dir);
    cs.load();
    for(int i = 0; i < 5; ++i)
    {
        cs.set_camera_answer("cam1", 1, i % 2 ? "Yes" : "No");
        cs.save();
    }
    RTF_ASSERT(_config_files(_top_dir) == vector<string>{"revere_cfg.json"});
}
