#include "test_main_window_address.h"
#include "main_window.h"
#include "r_ui_utils/font_catalog.h"
#include <string>
#include <vector>

using namespace revere;
using namespace std;

// test_plugin_status_line.cpp defines r_ui_utils::fonts for this executable.

REGISTER_TEST_FIXTURE(test_main_window_address);

namespace
{

// The real main window drawn headless with one recording camera selected, the mouse driven through ImGui's input
// events: the buttons of the camera's card are found by clicking across the detail pane.
struct window_ui
{
    ImGuiContext* ctx;
    main_window_fonts fonts {"24", "22", "18"};
    main_window_state state;
    r_ui_utils::log_tail log;
    main_window_actions actions;
    vector<sidebar_list_ui_item> recording;
    vector<int> confirmed;     // the index of each camera "It's my camera" was pressed for

    explicit window_ui(bool held)
    {
        ctx = ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1100.0f, 700.0f);
        auto* f = io.Fonts->AddFontDefault();
        for(auto key : {"24", "22", "18"})
            r_ui_utils::fonts[key] = r_ui_utils::font_catalog{f, f, f};
        unsigned char* px; int w, h;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);

        sidebar_list_ui_item cam;
        cam.label = "Garage";
        cam.sub_label = "10.0.0.93";
        cam.camera_id = "garage";
        cam.address_held = held;
        cam.address_since_ms = 1761258998001LL;
        cam.address_notice = "A different device answers at this camera's address.";
        recording.push_back(cam);

        // Every action does nothing but the one under test, so a click on any other button is harmless.
        auto none_i = [](int) {};
        actions.select_recording = actions.select_discovered = actions.view = actions.properties = actions.remove = actions.repair = none_i;
        actions.move_use = actions.move_keep = actions.move_ok = actions.record = actions.forget = none_i;
        actions.select_plugin = []() {};
        actions.set_plugin_enabled = [](const string&, bool) {};
        actions.uninstall_plugin = [](const string&, const string&) {};
        actions.run_plugin_action = [](const string&, const string&) {};
        actions.open_url = [](const string&) {};
        actions.add_rtsp_camera = []() {};
        actions.dismiss_notice = [](const string&) {};
        actions.address_confirm = [this](int i) { confirmed.push_back(i); };
    }
    ~window_ui() { ImGui::DestroyContext(ctx); }

    void frame()
    {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        main_window(30, 1100, 700, state, 0, -1, recording, {}, {}, "status", false, "Revere test", log, actions, fonts);
        ImGui::Render();
    }

    void click(float x, float y)
    {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(x, y);
        frame();
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
    }

    // Clicks on a grid across the detail pane (right of the list) until the button has been pressed; whether it was.
    bool click_until_confirmed()
    {
        frame();
        for(float y = 60.0f; y < 640.0f; y += 8.0f)
            for(float x = 330.0f; x < 1090.0f; x += 14.0f)
            {
                click(x, y);
                if(!confirmed.empty())
                    return true;
            }
        return false;
    }
};

}

void test_main_window_address::test_a_held_camera_has_its_my_camera_button()
{
    window_ui ui(true);
    RTF_ASSERT(ui.click_until_confirmed());
    RTF_ASSERT(ui.confirmed == vector<int>({0}));      // for the camera selected
}

void test_main_window_address::test_a_camera_that_is_not_held_has_none()
{
    window_ui ui(false);
    RTF_ASSERT(!ui.click_until_confirmed());
    RTF_ASSERT(ui.confirmed.empty());
}
