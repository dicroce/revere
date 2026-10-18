#include "test_section_state.h"
#include "r_ui_utils/section_state.h"

using namespace r_ui_utils;
using namespace std;
using json = nlohmann::json;

REGISTER_TEST_FIXTURE(test_section_state);

void test_section_state::test_round_trip()
{
    sections_open saved {{"recording", false}, {"plugins", true}};
    auto loaded = parse_sections_open(sections_open_to_json(saved));
    RTF_ASSERT(loaded == saved);
    RTF_ASSERT(!section_open(loaded, "recording", true));
    RTF_ASSERT(section_open(loaded, "plugins", false));
}

void test_section_state::test_nothing_saved()
{
    // First run, or a file from before sections were saved: every section
    // gets the caller's default.
    auto loaded = parse_sections_open(json());
    RTF_ASSERT(loaded.empty());
    RTF_ASSERT(section_open(loaded, "recording", true));
    RTF_ASSERT(!section_open(loaded, "discovered", false));
}

void test_section_state::test_bad_entries_skipped()
{
    // A hand-edited or damaged entry loses only that section's state.
    auto j = json::parse(R"({"recording": false, "discovered": "no", "plugins": 0, "": false, "log": null})");
    auto loaded = parse_sections_open(j);
    RTF_ASSERT(loaded.size() == 1);
    RTF_ASSERT(!section_open(loaded, "recording", true));
    RTF_ASSERT(section_open(loaded, "discovered", true));
    RTF_ASSERT(section_open(loaded, "plugins", true));
}

void test_section_state::test_not_an_object()
{
    RTF_ASSERT(parse_sections_open(json::array({false, true})).empty());
    RTF_ASSERT(parse_sections_open(json("recording")).empty());
    RTF_ASSERT(parse_sections_open(json(false)).empty());
}

void test_section_state::test_from_file_text()
{
    // The "sections" value as it sits in revere_cfg.json.
    auto cfg = json::parse(R"({"camera_answers": {}, "sections": {"discovered": false}})");
    auto loaded = parse_sections_open(cfg["sections"]);
    RTF_ASSERT(!section_open(loaded, "discovered", true));
    RTF_ASSERT(section_open(loaded, "recording", true));
}
