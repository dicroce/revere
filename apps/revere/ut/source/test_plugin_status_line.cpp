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
