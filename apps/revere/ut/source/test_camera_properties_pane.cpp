#include "test_camera_properties_pane.h"
#include "r_disco/r_substream.h"
#include "camera_properties_pane.h"
#include "r_ui_utils/font_catalog.h"
#include <string>

using namespace revere;
using namespace std;

// test_plugin_status_line.cpp defines r_ui_utils::fonts for this executable.

REGISTER_TEST_FIXTURE(test_camera_properties_pane);

namespace
{

// The camera properties pane drawn headless, with the mouse driven through ImGui's input events: the pills of "Record
// from" are found by clicking across the pane, since an item's place isn't known from outside.
struct pane_ui
{
    ImGuiContext* ctx;
    camera_properties_session session;
    streams_state streams;
    bool doorbell {false}, motion {false}, prune {false};
    string hours {"24"}, tier {"auto"}, tier_status, transport {"TCP"}, transport_status, record_from {"main"};
    bool available {false};

    pane_ui(const string& record_from_now, bool available_now) : ctx(ImGui::CreateContext()), record_from(record_from_now), available(available_now)
    {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1100.0f, 700.0f);
        auto* f = io.Fonts->AddFontDefault();
        for(auto key : {"24", "22", "18"})
            r_ui_utils::fonts[key] = r_ui_utils::font_catalog{f, f, f};
        g_dialog_fonts = main_window_fonts {"24", "22", "18"};
        unsigned char* px; int w, h;
        io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
        session.begin("cam", doorbell, motion, prune, hours, tier, transport, record_from);
    }
    ~pane_ui() { ImGui::DestroyContext(ctx); }

    void frame()
    {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(1100.0f, 700.0f));
        ImGui::Begin("pane", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        camera_properties_pane(session, doorbell, motion, prune, hours, tier, tier_status, transport, transport_status, record_from, available,
                               "/tmp/cam.nts", streams, false, [](const string&, const string&) {}, []() {}, []() {}, []() {});
        ImGui::End();
        ImGui::Render();
    }

    // Click at (x, y): move there, press, release, with a frame between each.
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

    // Clicks on a grid across the pane until `record_from` becomes `want`; whether it did.
    bool click_until(const string& want)
    {
        for(float y = 20.0f; y < 640.0f; y += 6.0f)
            for(float x = 10.0f; x < 1090.0f; x += 12.0f)
            {
                click(x, y);
                if(record_from == want)
                    return true;
            }
        return false;
    }
};

}

// "Smaller stream" is chosen with a click when the camera's smaller stream works, and can't be when it doesn't.
void test_camera_properties_pane::test_the_smaller_stream_can_be_chosen_only_when_it_works()
{
    {
        pane_ui ui("main", true);
        ui.frame();
        RTF_ASSERT(ui.click_until("sub"));
        RTF_ASSERT(ui.session.dirty(false, false, false, "auto", "TCP", ui.record_from, false));    // it counts as a change to save
    }
    {
        pane_ui ui("main", false);
        ui.frame();
        RTF_ASSERT(!ui.click_until("sub"));       // no click anywhere gets it
        RTF_ASSERT(ui.record_from == "main");
    }
}

// Set to the smaller stream already, it stays choosable (the stream may be away just now) and can go back to main.
void test_camera_properties_pane::test_a_choice_already_made_stays_choosable_and_can_go_back()
{
    for(bool available : {true, false})
    {
        pane_ui ui("sub", available);
        ui.frame();
        RTF_ASSERT(ui.click_until("main"));
        RTF_ASSERT(ui.record_from == "main");
        // And back again.
        RTF_ASSERT(ui.click_until("sub"));
    }
}
