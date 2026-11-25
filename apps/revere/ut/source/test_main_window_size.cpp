#include "test_main_window_size.h"
#include "main_window.h"
#include "r_ui_utils/font_catalog.h"
#include <vector>
#include <string>

using namespace revere;
using namespace std;

// test_plugin_status_line.cpp defines r_ui_utils::fonts for this executable.

REGISTER_TEST_FIXTURE(test_main_window_size);

namespace
{

// Draws the real main window headless, with ImGui's default font for all three font keys. ImGui asserts on a
// zero-size InvisibleButton, so a frame that gets through is the check.
struct window_frames
{
    ImGuiContext* ctx;
    main_window_fonts fonts {"24", "22", "18"};
    main_window_state state;
    r_ui_utils::log_tail log;
    main_window_actions actions;

    window_frames() : ctx(ImGui::CreateContext())
    {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(900.0f, 600.0f);
        auto* f = io.Fonts->AddFontDefault();
        for(auto key : {"24", "22", "18"})
            r_ui_utils::fonts[key] = r_ui_utils::font_catalog{f, f, f};
        unsigned char* px; int w, h;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        for(int i = 0; i < 40; ++i)
            log.add("a log line");
    }
    ~window_frames() { ImGui::DestroyContext(ctx); }

    void frame(uint16_t top, uint16_t w, uint16_t h)
    {
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)w, (float)h);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        sidebar_list_ui_item cam;
        cam.label = "Camera one";
        main_window(top, w, h, state, 0, -1, {cam}, {}, {}, "status", false, "Revere test", log, actions, fonts);
        ImGui::Render();
    }
};

}

void test_main_window_size::test_no_room_draws_without_asserting()
{
    window_frames ui;
    // No window yet (0 x 0), a window shorter than the menu and status bars, and one with a sliver of room.
    for(auto size : {make_pair(0, 0), make_pair(300, 30), make_pair(300, 60), make_pair(1, 1), make_pair(0, 400)})
    {
        for(bool log_open : {false, true})
        {
            ui.state.show_log = log_open;
            ui.frame(0, (uint16_t)size.first, (uint16_t)size.second);
            ui.frame(30, (uint16_t)size.first, (uint16_t)size.second);
        }
    }
}

void test_main_window_size::test_log_pane_taller_than_the_window()
{
    window_frames ui;
    ui.state.show_log = true;
    ui.state.log_height = 5000.0f;
    // Short windows, so the log's share leaves the rest nothing, then a normal size again.
    for(uint16_t h : {(uint16_t)120, (uint16_t)200, (uint16_t)90, (uint16_t)600})
        ui.frame(30, 640, h);
}
