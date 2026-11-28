#include "test_plugin_status_line.h"
#include "main_window.h"
#include "r_utils/3rdparty/json/json.h"
#include <functional>

using namespace revere;
using namespace std;
using json = nlohmann::json;

// The app defines the font catalog in main.cpp; these tests draw with ImGui's default font.
std::unordered_map<std::string, r_ui_utils::font_catalog> r_ui_utils::fonts;

REGISTER_TEST_FIXTURE(test_plugin_status_line);

namespace
{

// plugin_status_lines drawn headless, with the mouse driven through ImGui's input events.
struct headless_ui
{
    ImGuiContext* ctx;
    vector<string> ran;
    ImVec2 last_item;   // the centre of the last item drawn (the line's last button)
    ImVec2 last_min;    // and its top left

    headless_ui() : ctx(ImGui::CreateContext())
    {
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(900.0f, 600.0f);
        io.IniFilename = nullptr;
        unsigned char* px; int w, h;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    }
    ~headless_ui() { ImGui::DestroyContext(ctx); }

    void frame(const string& message)
    {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f));
        ImGui::Begin("plugin");
        plugin_status_lines(message, [](const string&) {}, [this](const string& a) { ran.push_back(a); }, main_window_fonts{});
        auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        last_item = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        last_min = a;
        ImGui::End();
        ImGui::Render();
    }

    // The button just left of the last one, on its row (the question's Cancel).
    ImVec2 left_of_last(const char* label) const
    {
        float w = main_window_detail::button_size(label).x;
        return ImVec2(last_min.x - ImGui::GetStyle().ItemSpacing.x - w * 0.5f, last_item.y);
    }

    // A click on the last button drawn (or at `at`), then a frame to show what follows.
    void click(const string& message) { click_at(message, last_item); }
    void click_at(const string& message, ImVec2 at)
    {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(at.x, at.y);
        frame(message);
        io.AddMouseButtonEvent(0, true);
        frame(message);
        io.AddMouseButtonEvent(0, false);
        frame(message);
        io.AddMousePosEvent(-1.0f, -1.0f);
        frame(message);
    }
};

string _message(const json& line)
{
    return json{{"lines", json::array({line})}}.dump();
}

}

void test_plugin_status_line::test_action_line()
{
    plugin_status_line_view v;
    RTF_ASSERT(parse_plugin_status_line(json{{"text", "Front door: in Apple Home"}, {"action", "reset:revere_1"},
                                             {"action_label", "Reset pairing"}, {"confirm", "Remove it from Apple Home?"}}, v));
    RTF_ASSERT(v.text == "Front door: in Apple Home");
    RTF_ASSERT(v.action == "reset:revere_1");
    RTF_ASSERT(v.action_label == "Reset pairing");
    RTF_ASSERT(v.confirm == "Remove it from Apple Home?");

    // No question: the button runs the action at once.
    plugin_status_line_view w;
    RTF_ASSERT(parse_plugin_status_line(json{{"text", "t"}, {"action", "a"}, {"action_label", "Go"}}, w));
    RTF_ASSERT(w.action == "a" && w.confirm.empty());
}

void test_plugin_status_line::test_action_needs_a_label()
{
    // An action without a label, or a label without an action, draws no button; the text still shows.
    plugin_status_line_view v;
    RTF_ASSERT(parse_plugin_status_line(json{{"text", "t"}, {"action", "a"}, {"confirm", "Sure?"}}, v));
    RTF_ASSERT(v.text == "t" && v.action.empty() && v.action_label.empty() && v.confirm.empty());

    plugin_status_line_view w;
    RTF_ASSERT(parse_plugin_status_line(json{{"text", "t"}, {"action_label", "Go"}}, w));
    RTF_ASSERT(w.action.empty() && w.action_label.empty());

    // Not strings: ignored.
    plugin_status_line_view x;
    RTF_ASSERT(parse_plugin_status_line(json{{"text", "t"}, {"action", 3}, {"action_label", true}}, x));
    RTF_ASSERT(x.action.empty() && x.action_label.empty());
}

void test_plugin_status_line::test_plain_lines_unchanged()
{
    plugin_status_line_view v;
    RTF_ASSERT(parse_plugin_status_line(json{{"text", "Code 123-45-678"}, {"copy", "123-45-678"}}, v));
    RTF_ASSERT(v.copy == "123-45-678" && v.action.empty());

    plugin_status_line_view q;
    RTF_ASSERT(parse_plugin_status_line(json{{"qr", "X-HM://ABC"}, {"action", "a"}, {"action_label", "Go"}}, q));
    RTF_ASSERT(q.qr == "X-HM://ABC" && q.action.empty());
}

void test_plugin_status_line::test_button_runs_action()
{
    headless_ui ui;
    auto m = _message({{"text", "Cam"}, {"action", "go:1"}, {"action_label", "Go"}});
    ui.frame(m);
    RTF_ASSERT(ui.ran.empty());
    ui.click(m);
    RTF_ASSERT(ui.ran == vector<string>{"go:1"});
}

void test_plugin_status_line::test_confirm_asks_first()
{
    headless_ui ui;
    auto m = _message({{"text", "Cam"}, {"action", "reset:1"}, {"action_label", "Reset pairing"}, {"confirm", "Remove it?"}});
    ui.frame(m);
    ui.click(m);                    // the button: only the question appears
    RTF_ASSERT(ui.ran.empty());
    ui.click(m);                    // the question's own Reset pairing (drawn last)
    RTF_ASSERT(ui.ran == vector<string>{"reset:1"});

    // Asked again, then Cancel: nothing runs, and the button is back (one click asks again).
    ui.click(m);
    RTF_ASSERT(ui.ran.size() == 1);
    ui.click_at(m, ui.left_of_last("Cancel"));
    RTF_ASSERT(ui.ran.size() == 1);
    ui.click(m);                    // the button again: asks, doesn't run
    RTF_ASSERT(ui.ran.size() == 1);
}

void test_plugin_status_line::test_question_dropped_with_its_line()
{
    headless_ui ui;
    auto m = _message({{"text", "Cam"}, {"action", "reset:1"}, {"action_label", "Reset pairing"}, {"confirm", "Remove it?"}});
    ui.frame(m);
    ui.click(m);                    // asking
    ui.frame(json{{"lines", json::array()}}.dump());   // the line goes away for a frame
    ui.frame(m);                    // and comes back: the button, not the question
    ui.click(m);
    RTF_ASSERT(ui.ran.empty());     // that click only asked again
    ui.click(m);
    RTF_ASSERT(ui.ran == vector<string>{"reset:1"});
}

namespace
{

// A camera's choice line: the summary text and a radio list of three modes, `value` selected.
json _choice_line(const string& value, const string& live_name = "Live", bool with_cost = true)
{
    return {{"text", "Porch"},
            {"choice", {{"value", value}, {"action", "mode:porch:"},
                        {"options", json::array({
                            {{"value", "off"}, {"name", "Off"}, {"subtitle", "Not in Apple Home"}, {"cost", ""}, {"warn", false}},
                            {{"value", "live"}, {"name", live_name}, {"subtitle", "Live view and snapshots"}, {"cost", ""}, {"warn", false}},
                            {{"value", "apple_recording"}, {"name", "Live + Apple alerts + Apple recording"},
                             {"subtitle", "Clips in Apple Home"}, {"cost", with_cost ? "May be paused at busy times" : ""}, {"warn", with_cost}}})}}}};
}

}

void test_plugin_status_line::test_choice_line()
{
    plugin_status_line_view v;
    RTF_ASSERT(parse_plugin_status_line(_choice_line("live"), v));
    RTF_ASSERT(v.text == "Porch");
    RTF_ASSERT(v.choice_value == "live" && v.choice_action == "mode:porch:");
    RTF_ASSERT(v.options.size() == 3);
    RTF_ASSERT(v.options[0].value == "off" && v.options[0].name == "Off" && v.options[0].subtitle == "Not in Apple Home" && v.options[0].cost.empty() && !v.options[0].warn);
    RTF_ASSERT(v.options[2].value == "apple_recording" && v.options[2].name == "Live + Apple alerts + Apple recording");
    RTF_ASSERT(v.options[2].subtitle == "Clips in Apple Home" && v.options[2].cost == "May be paused at busy times" && v.options[2].warn);
    RTF_ASSERT(!v.warn);

    // A plain line in the warning tone.
    plugin_status_line_view w;
    RTF_ASSERT(parse_plugin_status_line(json{{"text", "Recording paused"}, {"warn", true}}, w));
    RTF_ASSERT(w.warn && w.options.empty());
    plugin_status_line_view x;
    RTF_ASSERT(parse_plugin_status_line(json{{"text", "t"}, {"warn", "yes"}}, x));
    RTF_ASSERT(!x.warn);
}

void test_plugin_status_line::test_choice_that_cannot_be_drawn()
{
    // No action, or no usable option: the text still shows and nothing is drawn as a choice, as on a host from before.
    auto bare = [](json choice) {
        plugin_status_line_view v;
        RTF_ASSERT(parse_plugin_status_line(json{{"text", "Porch: Live"}, {"choice", choice}}, v));
        RTF_ASSERT(v.text == "Porch: Live" && v.options.empty() && v.choice_action.empty());
    };
    bare(json{{"value", "a"}, {"options", json::array({json{{"value", "a"}, {"name", "A"}}})}});          // no action
    bare(json{{"value", "a"}, {"action", ""}, {"options", json::array({json{{"value", "a"}, {"name", "A"}}})}});
    bare(json{{"value", "a"}, {"action", "m:"}, {"options", json::array()}});
    bare(json{{"value", "a"}, {"action", "m:"}});
    bare(json{{"value", "a"}, {"action", "m:"}, {"options", "A"}});
    bare(json{{"value", "a"}, {"action", 3}, {"options", json::array({json{{"value", "a"}, {"name", "A"}}})}});
    bare(json::array());
    bare(json{{"action", "m:"}, {"options", json::array({json{{"name", "no value"}}, json{{"value", "v"}}, 3})}});

    // An option that can't be used is skipped without dropping the rest.
    plugin_status_line_view v;
    RTF_ASSERT(parse_plugin_status_line(json{{"text", "t"}, {"choice", {{"value", "b"}, {"action", "m:"},
        {"options", json::array({json{{"name", "no value"}}, json{{"value", "v"}}, 3, json{{"value", "b"}, {"name", "B"}}})}}}}, v));
    RTF_ASSERT(v.options.size() == 1 && v.options[0].value == "b");
}

void test_plugin_status_line::test_choice_pick_runs_action()
{
    headless_ui ui;
    auto m = _message(_choice_line("live"));
    ui.frame(m);
    RTF_ASSERT(ui.ran.empty());

    // The last row (not selected): the choice's action and its value.
    ui.click(m);
    RTF_ASSERT((ui.ran == vector<string>{"mode:porch:apple_recording"}));

    // With no cost line the rows are one height, so the ones above the last are a step each from it.
    ui.ran.clear();
    auto even = _message(_choice_line("live", "Live", false));
    ui.frame(even);
    const ImVec2 last = ui.last_item;
    const float step = 2.0f * (last.y - ui.last_min.y) + ImGui::GetStyle().ItemSpacing.y;

    // The row above is the selected one: picking what is already chosen runs nothing.
    ui.click_at(even, ImVec2(last.x, last.y - step));
    RTF_ASSERT(ui.ran.empty());

    // Two above is the first, "Off".
    ui.click_at(even, ImVec2(last.x, last.y - 2.0f * step));
    RTF_ASSERT((ui.ran == vector<string>{"mode:porch:off"}));
}

void test_plugin_status_line::test_choice_names_wrap()
{
    // A long name takes more lines, not more width: its row is taller than a short name's, so none of it is cut off.
    headless_ui short_ui;
    short_ui.frame(_message(_choice_line("off", "Live")));
    const float short_height = short_ui.last_item.y - short_ui.last_min.y;

    string long_name;
    for(int i = 0; i < 80; ++i)
        long_name += "Live + Apple alerts + Apple recording ";
    headless_ui long_ui;
    long_ui.frame(_message(_choice_line("off", long_name)));
    // The last row is the third option (unchanged), so compare the whole block instead: the long name pushes it down.
    RTF_ASSERT(long_ui.last_min.y > short_ui.last_min.y + short_height * 3.0f);
}

void test_plugin_status_line::test_choice_ring_is_for_the_selected_row()
{
    using namespace r_ui_utils::theme;
    auto same = [](const ImVec4& a, const ImVec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; };
    RTF_ASSERT(same(plugin_choice_ring(true, false), accent));
    RTF_ASSERT(same(plugin_choice_ring(true, true), accent));
    RTF_ASSERT(same(plugin_choice_ring(false, false), border_strong));
    // A row the mouse is on, not selected, has no accent ring: it isn't a second selection.
    RTF_ASSERT(same(plugin_choice_ring(false, true), border_strong));
}

namespace
{

// A list of two cameras: the first's detail is a line of text, the second's has a button with a question.
json _list_line()
{
    return {{"list", {{"back", "Apple Home"}, {"rows", json::array({
        {{"id", "a"}, {"name", "Front Door"}, {"value", "Live"}, {"state", "In Apple Home"}, {"tone", "ok"},
         {"detail", json::array({{{"text", "Front Door detail"}}})}},
        {{"id", "b"}, {"name", "Garage"}, {"value", "Off"}, {"state", "Not added yet"}, {"tone", "warn"},
         {"detail", json::array({{{"text", "Garage detail"}, {"action", "reset:b"}, {"action_label", "Remove"}}})}}})}}}};
}

}

void test_plugin_status_line::test_list_line()
{
    plugin_status_line_view v;
    RTF_ASSERT(parse_plugin_status_line(_list_line(), v));
    RTF_ASSERT(v.is_list && v.back == "Apple Home" && v.rows.size() == 2);
    RTF_ASSERT(v.rows[0].id == "a" && v.rows[0].name == "Front Door" && v.rows[0].value == "Live");
    RTF_ASSERT(v.rows[0].state == "In Apple Home" && v.rows[0].tone == "ok");
    RTF_ASSERT(v.rows[0].detail.is_array() && v.rows[0].detail.size() == 1);

    // A row with no id or name, a repeated id and a row that isn't an object are left out; a detail that isn't a list is none.
    plugin_status_line_view w;
    RTF_ASSERT(parse_plugin_status_line(json{{"list", {{"rows", json::array({
        json{{"id", "a"}}, json{{"name", "no id"}}, 3, json{{"id", "b"}, {"name", "B"}, {"detail", "text"}},
        json{{"id", "b"}, {"name", "B again"}}})}}}}, w));
    RTF_ASSERT(w.rows.size() == 1 && w.rows[0].id == "b" && w.rows[0].name == "B");
    RTF_ASSERT(w.rows[0].detail.is_array() && w.rows[0].detail.empty());

    // No row to draw, or not a list of rows: the line is dropped.
    plugin_status_line_view x;
    RTF_ASSERT(!parse_plugin_status_line(json{{"list", {{"rows", json::array()}}}}, x));
    RTF_ASSERT(!parse_plugin_status_line(json{{"list", {{"rows", "a"}}}}, x));
    RTF_ASSERT(!parse_plugin_status_line(json{{"list", json::array()}}, x));
}

void test_plugin_status_line::test_list_row_opens_its_detail()
{
    headless_ui ui;
    auto m = _message(_list_line());
    ui.frame(m);
    RTF_ASSERT(ui.ran.empty());

    // The last row is Garage: picking it opens its detail, which is a button that runs the plugin's action.
    ui.click(m);
    RTF_ASSERT(ui.ran.empty());
    ui.frame(m);
    ui.click(m);
    RTF_ASSERT((ui.ran == vector<string>{"reset:b"}));
}

void test_plugin_status_line::test_list_back_and_vanished_row()
{
    headless_ui ui;
    auto m = _message(_list_line());
    ui.frame(m);
    ui.click(m);                    // Garage opened
    ui.frame(m);

    // Back is the first button in the window (under its title bar): the list shows again, so the last item is a row, not Garage's button.
    ui.click_at(m, ImVec2(24.0f, 38.0f));
    ui.click(m);                    // the last row again: opens Garage, runs nothing
    RTF_ASSERT(ui.ran.empty());

    // The open row goes away: the list shows, rather than an empty detail.
    auto only_a = _list_line();
    only_a["list"]["rows"].erase(1);
    auto m2 = _message(only_a);
    ui.frame(m2);
    ui.click(m2);                   // the last (only) row is Front Door: opens it, runs nothing
    RTF_ASSERT(ui.ran.empty());
    ui.frame(m2);
    ui.click(m2);                   // Front Door's detail is text only: nothing to run
    RTF_ASSERT(ui.ran.empty());
}

void test_plugin_status_line::test_a_row_that_went_away_does_not_come_back_open()
{
    headless_ui ui;
    auto both = _message(_list_line());
    auto only_a_line = _list_line();
    only_a_line["list"]["rows"].erase(1);
    auto only_a = _message(only_a_line);
    ui.frame(both);
    ui.click(both);                 // Garage opened
    ui.frame(both);
    ui.frame(only_a);               // Garage goes away: the list shows
    ui.frame(both);                 // and comes back: still the list, not its detail again
    ui.click(both);                 // the last row: opens Garage, and runs nothing (its button isn't there yet)
    RTF_ASSERT(ui.ran.empty());
    ui.frame(both);
    ui.click(both);                 // now its detail is open: the button runs the action
    RTF_ASSERT((ui.ran == vector<string>{"reset:b"}));
}
