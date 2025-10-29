
#include "r_utils/r_file.h"
#include "r_utils/r_socket.h"

#ifdef IS_WINDOWS
#include <windows.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "stb_image.h"

#include <string>
#include <thread>
#include <chrono>
#include <mutex>

#include "r_utils/r_string_utils.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_blocking_q.h"
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_gst_source.h"
#include "r_av/r_video_decoder.h"
#include "r_ui_utils/font_catalog.h"
#include "r_ui_utils/wizard.h"
#include "r_ui_utils/texture_loader.h"
#include "r_ui_utils/svg_icons.h"

// Define the fonts global variable for this app
std::unordered_map<std::string, r_ui_utils::font_catalog> r_ui_utils::fonts;

// Global texture IDs for analytics icons
static GLuint g_person_texture_id = 0;
static GLuint g_car_texture_id = 0;

// Accessor functions for analytics renderer
GLuint get_person_texture_id() { return g_person_texture_id; }
GLuint get_car_texture_id() { return g_car_texture_id; }

#include "configure_state.h"
#include "gl_utils.h"
#include "imgui_ui.h"
#include "utils.h"
#include "layouts.h"
#include "query.h"
#include "pipeline_host.h"
#include "error_handling.h"

#include "V_32x32.h"
// V_32x32_png && V_32x32_png_len

using namespace std;
using namespace r_utils;
using namespace vision;

// Create RGBA texture directly with alpha channel support
static void create_svg_texture_direct(const r_ui_utils::icon_bitmap& icon, GLuint& texture_id)
{
    if (icon.pixels.empty()) return;
    
    // Use RGBA data directly (keep alpha channel for transparency)
    gl_safe::create_texture_rgba(&texture_id, icon.width, icon.height, icon.pixels.data());
}

struct revere_update
{
    vector<vision::sidebar_list_ui_item> cameras;
    r_nullable<string> status_text;
};

struct vision_ui_state
{
    int selected_item {-1};
    revere_update current_revere_update;
    main_client_state mcs;
};

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

extern ImGuiContext *GImGui;

void configure_configure_wizard(
    r_ui_utils::wizard& configure_wizard,
    vision::configure_state& cfg_state,
    GLFWwindow* window,
    vision_ui_state& ui_state
)
{
    static string ip_storage;
    ip_storage = (!cfg_state.get_revere_ipv4().is_null())?cfg_state.get_revere_ipv4().value():string();
    configure_wizard.add_step(
        "configure",
        [&](){
            ImGui::OpenPopup("Configure");
            vision::configure_modal(
                GImGui,
                "Configure",
                ip_storage,
                cfg_state,
                [&](){
                    configure_wizard.cancel();
                    cfg_state.set_revere_ipv4(ip_storage);
                    cfg_state.save();
                },
                [&](){
                    configure_wizard.cancel();
                    cfg_state.load();
                }
            );
        }
    );

    configure_wizard.add_step(
        "export_failure",
        [&](){
            ImGui::OpenPopup("Error");
            vision::error_modal(
                GImGui,
                "Error",
                "Error encountered while exporting video.",
                [&](){
                    ui_state.mcs.obos.cbs.exp_state = EXPORT_STATE_NONE;
                    configure_wizard.cancel();
                }
            );
        }
    );
}

void reconfigure_control_bar(configure_state& cfg, pipeline_host& ph, vision_ui_state& ui_state)
{
    ui_state.mcs.obos.cbs.live();
}

void exit_layout(configure_state& cfg, pipeline_host& ph)
{
    ph.destroy_video_textures();
}

void enter_layout(configure_state& cfg, pipeline_host& ph, vision_ui_state& ui_state, layout l)
{
    switch(l)
    {
        case vision::LAYOUT_ONE_BY_ONE:
            R_LOG_INFO("LAYOUT_ONE_BY_ONE");
        break;
        case vision::LAYOUT_TWO_BY_TWO:
            R_LOG_INFO("LAYOUT_TWO_BY_TWO");
        break;
        case vision::LAYOUT_FOUR_BY_FOUR:
            R_LOG_INFO("LAYOUT_FOUR_BY_FOUR");
        break;
        default:
            R_LOG_INFO("UNKNOWN");
        break;
    }
}

void change_layout(configure_state& cfg, pipeline_host& ph, vision_ui_state& ui_state, layout l)
{
    exit_layout(cfg, ph);
    cfg.set_current_layout(l);
    ph.change_layout(0, l);
    enter_layout(cfg, ph, ui_state, l);
}

void change_one_by_one_config_and_layout(configure_state& cfg, pipeline_host& ph, vision_ui_state& ui_state, int window, const string& label, const string& camera_id, bool do_motion_detection)
{
    auto maybe_revere_ip = cfg.get_revere_ipv4();
    if (maybe_revere_ip.is_null())
        return;

    stream_info si;
    si.name = cfg.make_name(window, LAYOUT_ONE_BY_ONE, 0);

    auto url_label = label;
    replace(begin(url_label), end(url_label), ' ', '_');

    si.rtsp_url = r_string_utils::format("rtsp://%s:10554/%s", maybe_revere_ip.value().c_str(), url_label.c_str());
    si.camera_id = camera_id;
    si.do_motion_detection = do_motion_detection;

    cfg.set_stream_info(si);

    change_layout(cfg, ph, ui_state, LAYOUT_ONE_BY_ONE);

    reconfigure_control_bar(cfg, ph, ui_state);
}

std::thread system_info_thread(
    r_blocking_q<revere_update>& update_q,
    configure_state& cfg,
    bool& close_requested,
    bool& update_ui
)
{
    return std::thread([&](){
        while(!close_requested)
        {
            revere_update ru;

            try
            {
                auto maybe_ip = cfg.get_revere_ipv4();
                if(update_ui && !maybe_ip.is_null())
                {

                    auto result = query_cameras(cfg.get_revere_ipv4().value());

                    {
                        ru.cameras = result;
                        ru.status_text = "Connected to Revere.";
                        update_q.post(ru);
                        update_ui = false;
                    }
                }
            }
            catch(const std::exception& e)
            {
                ru.cameras.clear();
                ru.status_text = "Unable to communicate with Revere.";
                update_q.post(ru);
                R_LOG_EXCEPTION_AT(e, __FILE__, __LINE__);
            }
            catch(...)
            {
                ru.cameras.clear();
                ru.status_text = "Unable to communicate with Revere.";
                update_q.post(ru);
                R_LOG_ERROR("Unknown Revere Communications Error");
            }

            this_thread::sleep_for(chrono::microseconds(1000000));
        }
    });
}

void _pop_export_folder()
{
    auto export_dir = sub_dir("exports");

#ifdef IS_WINDOWS
    ShellExecuteA(NULL, "open", export_dir.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#endif
#ifdef IS_LINUX
    system(r_string_utils::format("xdg-open %s", export_dir.c_str()).c_str());
#endif
}

void _set_working_dir()
{
    // Set the current working directory to the directory of the executable
    auto exe_path = r_fs::current_exe_path();
    auto wd = exe_path.substr(0, exe_path.find_last_of(PATH_SLASH));
    R_LOG_INFO("wd: %s", wd.c_str());
    r_fs::change_working_directory(wd);
}

#ifdef IS_WINDOWS
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
#endif
#ifdef IS_LINUX
int main(int, char**)
#endif
{
    // Some notes for understanding vision.
    // 1. Vision is a UI that has the concept of layouts. Layout's in this case are referring to a collection of visible cameras.
    // 2. Each stream in a layout has a unique string name (examples: 0_onebyone_0 or 0_twobytwo_1). The first number in the name is the screen numbers. This 
    // is mostly to support future secondary vision windows. Then we have the layout name and finally we have the index of the stream within the layout.
    // 3. The current configuration of all layouts is stored in the config json file. When cameras are dragged to layout locations the file is resaved. This allows
    // vision to remember its current configuration from run to run.
    // 4. The file configure_state handles loading and saving the current configuration. The configuration is considered the master source of what we SHOULD be
    // doing.
    // 5. The file pipeline_host owns all the pipelines. The pipelines it holds are created on entry to a layout and are destroyed when we go to another layout.
    // 6. Periodically we reconcile the current configuration with the pipeline host and start or stop pipelines as needed.
    // 7. When a pipeline is detected to be dead we simply remove it from the pipeline host and let it get restarted.
    // 8. The file pipeline_state owns and controls a single pipeline... the pipeline_host has a colleciton of pipeline_states.

    r_utils::r_raw_socket::socket_startup();

    r_logger::install_logger(r_fs::platform_path(vision::sub_dir("logs")), "vision_log_");

    r_logger::install_terminate();

    R_LOG_INFO("Vision Starting...");

    _set_working_dir();

    r_pipeline::gstreamer_init();

    auto top_dir = vision::top_dir();

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vision", NULL, NULL);
    if (window == NULL)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    GLFWimage images[1];
    int x, y, channels;
    // V_32x32_png && V_32x32_png_len
    images[0].pixels = stbi_load_from_memory(V_32x32_png, V_32x32_png_len, &x, &y, &channels, 4);
    images[0].width = x;
    images[0].height = y;

    glfwSetWindowIcon(window, 1, images);
    stbi_image_free(images[0].pixels);

    bool close_requested = false;

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    r_ui_utils::load_fonts(io, 24, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, 22, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, 20, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, 18, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, 16, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, 14, r_ui_utils::fonts);

    {
        vision_ui_state ui_state;
        vision::configure_state cfg_state;
        cfg_state.load();

        r_ui_utils::wizard configure_wizard;
        configure_configure_wizard(configure_wizard, cfg_state, window, ui_state);

        pipeline_host ph(cfg_state);
        ph.start();

        change_layout(cfg_state, ph, ui_state, cfg_state.get_current_layout());
        reconfigure_control_bar(cfg_state, ph, ui_state);

        bool update_ui = true;

        r_blocking_q<revere_update> update_q;
        auto comm_thread = system_info_thread(
            update_q,
            cfg_state,
            close_requested,
            update_ui
        );

        auto last_ui_update_ts = chrono::steady_clock::now();

        // Create SVG textures directly after ImGui initialization
        R_LOG_INFO("Creating SVG icon textures...");
        // Use black color in RGBA format: R=0, G=0, B=0, A=255
        // Make icons larger: 48x48 (50% bigger than 32x32)
        auto person_bitmap = r_ui_utils::render_svg_to_bitmap(r_ui_utils::svg_icons::person, 48, 0x000000FF);
        auto car_bitmap = r_ui_utils::render_svg_to_bitmap(r_ui_utils::svg_icons::car, 48, 0x000000FF);
        
        create_svg_texture_direct(person_bitmap, g_person_texture_id);
        create_svg_texture_direct(car_bitmap, g_car_texture_id);
        
        R_LOG_INFO("SVG textures created: person=%u, car=%u", g_person_texture_id, g_car_texture_id);
        
        r_ui_utils::texture_loader tl;

        // Main loop
        while(!close_requested)
        {
            auto now = chrono::steady_clock::now();

            if(now - last_ui_update_ts > chrono::seconds(5))
            {
                update_ui = true;
                last_ui_update_ts = now;
            }

            glfwWaitEventsTimeout(0.1);

            if(glfwWindowShouldClose(window) != GL_FALSE)
            {
                close_requested = true;
                continue;
            }

            auto update = update_q.poll(chrono::milliseconds(1));

            if(!update.is_null())
                ui_state.current_revere_update = update.value();

            // Start the Dear ImGui frame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            ImGui::PushFont(r_ui_utils::fonts["24.00"].roboto_regular);

            auto client_top = vision::main_menu(
                [&close_requested](){
                    close_requested = true;
                },
                [&configure_wizard](){
                    configure_wizard.next("configure");
                },
                [&cfg_state, &ph, &ui_state](){change_layout(cfg_state, ph, ui_state, LAYOUT_ONE_BY_ONE);},
                [&cfg_state, &ph, &ui_state](){change_layout(cfg_state, ph, ui_state, LAYOUT_TWO_BY_TWO);},
                [&cfg_state, &ph, &ui_state](){change_layout(cfg_state, ph, ui_state, LAYOUT_FOUR_BY_FOUR);}
            );

            auto window_size = ImGui::GetIO().DisplaySize;
            auto window_width = (uint16_t)window_size.x;
            auto window_height = (uint16_t)window_size.y;

            uint16_t left_panel_width = 0;

            vision::main_layout(
                client_top,
                window_width,
                window_height,
                "Cameras",
                [&](uint16_t pw){
                    left_panel_width = pw;
                    sidebar_list(
                        GImGui,
                        cfg_state,
                        ui_state.selected_item,
                        pw,
                        ui_state.current_revere_update.cameras,
                        [&](int idx, bool double_clicked){
                            ui_state.selected_item = idx;
                            if(double_clicked)
                            {
                                change_one_by_one_config_and_layout(
                                    cfg_state,
                                    ph,
                                    ui_state,
                                    0,
                                    ui_state.current_revere_update.cameras[idx].label,
                                    ui_state.current_revere_update.cameras[idx].camera_id,
                                    ui_state.current_revere_update.cameras[idx].do_motion_detection
                                );
                            }
                        }
                    );
                },
                "Main",
                [&](uint16_t main_width, uint16_t main_height){
                    main_client(
                        cfg_state.get_current_layout(),
                        left_panel_width,
                        client_top,
                        main_width,
                        main_height,
                        0,
                        ui_state.mcs,
                        cfg_state,
                        [&](uint16_t x, uint16_t y, uint16_t w, uint16_t h, const std::string& name){
                            // Validate container dimensions
                            if (!state_validate::is_valid_frame_dimensions(w, h))
                            {
                                R_LOG_ERROR("Invalid container dimensions for stream %s: %dx%d", name.c_str(), w, h);
                                // Render placeholder image
                                ImGui::Image((void*)(intptr_t)0, ImVec2(w, h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                                return;
                            }
                            
                            auto maybe_rc = ph.lookup_render_context(name, w, h);

                            if(!maybe_rc.is_null())
                            {
                                auto rc = maybe_rc.value();
                                
                                // Validate render context dimensions
                                if (!state_validate::is_valid_frame_dimensions(rc->w, rc->h))
                                {
                                    R_LOG_ERROR("Invalid render context dimensions for stream %s: %dx%d", name.c_str(), rc->w, rc->h);
                                    ImGui::Image((void*)(intptr_t)0, ImVec2(w, h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                                    return;
                                }
                                
                                // Validate texture ID
                                if (!state_validate::is_valid_texture_id(rc->texture_id))
                                {
                                    R_LOG_ERROR("Invalid texture ID for stream %s: %u", name.c_str(), rc->texture_id);
                                    ImGui::Image((void*)(intptr_t)0, ImVec2(w, h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                                    return;
                                }
                                
                                // Update playhead position based on current video timestamp during playback
                                if (ph.playing() && name == "0_onebyone_0")
                                {
                                    if (rc->pts > 0)
                                    {
                                        // PTS is now absolute timestamp (milliseconds since epoch)
                                        auto frame_time = std::chrono::system_clock::time_point(std::chrono::milliseconds(rc->pts));
                                        
                                        
                                        // Check if timestamp is within current timeline range
                                        if (ui_state.mcs.obos.cbs.tr.time_is_in_range(frame_time))
                                        {
                                            // Convert timestamp to playhead position (0-1000)
                                            int new_playhead_pos = ui_state.mcs.obos.cbs.tr.time_to_range(frame_time, 0, 1000);
                                            
                                            // Update playhead position if not currently dragging and position is valid
                                            if (!ui_state.mcs.obos.cbs.dragging && state_validate::is_valid_playhead_position(new_playhead_pos))
                                            {
                                                // If we're in live mode, don't let playhead move backwards from live position
                                                bool is_at_live = (ui_state.mcs.obos.cbs.playhead_pos >= timeline_constants::PLAYHEAD_MAX_POSITION);
                                                if (is_at_live)
                                                {
                                                    ui_state.mcs.obos.cbs.playhead_pos = timeline_constants::PLAYHEAD_MAX_POSITION;
                                                }
                                                else
                                                {
                                                    ui_state.mcs.obos.cbs.playhead_pos = new_playhead_pos;
                                                }
                                            }
                                        }
                                    }
                                }
                                
                                // Calculate the scaling factor to fit the video in the container while maintaining aspect ratio
                                auto scale_x_maybe = state_validate::safe_divide((float)w, (float)rc->w);
                                auto scale_y_maybe = state_validate::safe_divide((float)h, (float)rc->h);
                                
                                if (!scale_x_maybe.has_value() || !scale_y_maybe.has_value())
                                {
                                    R_LOG_ERROR("Invalid scaling calculation for stream %s", name.c_str());
                                    ImGui::Image((void*)(intptr_t)0, ImVec2(w, h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                                    return;
                                }
                                
                                float scale = std::min(scale_x_maybe.value(), scale_y_maybe.value());
                                
                                // Calculate the scaled dimensions with safe arithmetic
                                if (!state_validate::is_safe_multiplication((int64_t)rc->w, (int64_t)(scale * 1000)) ||
                                    !state_validate::is_safe_multiplication((int64_t)rc->h, (int64_t)(scale * 1000)))
                                {
                                    R_LOG_ERROR("Scaling calculation overflow for stream %s", name.c_str());
                                    ImGui::Image((void*)(intptr_t)0, ImVec2(w, h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                                    return;
                                }
                                
                                uint16_t scaled_w = (uint16_t)(rc->w * scale);
                                uint16_t scaled_h = (uint16_t)(rc->h * scale);
                                
                                // Validate scaled dimensions
                                if (!state_validate::is_valid_frame_dimensions(scaled_w, scaled_h))
                                {
                                    R_LOG_ERROR("Invalid scaled dimensions for stream %s: %dx%d", name.c_str(), scaled_w, scaled_h);
                                    ImGui::Image((void*)(intptr_t)0, ImVec2(w, h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                                    return;
                                }
                                
                                // Center the video in the container
                                uint16_t lx = (w > scaled_w) ? ((w - scaled_w) / 2) : 0;
                                uint16_t ly = (h > scaled_h) ? ((h - scaled_h) / 2) : 0;
                                
                                // Validate coordinates
                                if (!state_validate::is_valid_coordinates((float)lx, (float)ly, (float)w, (float)h))
                                {
                                    R_LOG_ERROR("Invalid centering coordinates for stream %s: %d,%d", name.c_str(), lx, ly);
                                    lx = ly = 0; // Fallback to top-left positioning
                                }
                                
                                // SetCursorPos uses window-relative coordinates, not absolute coordinates
                                ImGui::SetCursorPos(ImVec2((float)lx, (float)ly));
                                ImGui::Image((void*)(intptr_t)rc->texture_id, ImVec2((float)scaled_w, (float)scaled_h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                            }
                            else
                            {
                                // Render placeholder with validated dimensions
                                if (state_validate::is_valid_frame_dimensions(w, h))
                                {
                                    ImGui::Image((void*)(intptr_t)0, ImVec2(w, h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                                }
                                else
                                {
                                    R_LOG_ERROR("Invalid placeholder dimensions: %dx%d", w, h);
                                    // Use minimum valid dimensions for placeholder
                                    ImGui::Image((void*)(intptr_t)0, ImVec2(100, 100), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f));
                                }
                            }

                            // Note: The BeginPopupContextItem() applies to the last widget we have emitted. At this point
                            // we have emitted the image, so we want to apply the popup to the image.
                            if(ImGui::BeginPopupContextItem("popup_menu"))
                            {
                                if(ImGui::Selectable("Disconnect"))
                                {
                                    cfg_state.unset_stream_info(name);
                                    ph.disconnect_stream(0, name);
                                }
                                ImGui::EndPopup();
                            }

                            if(ImGui::BeginDragDropTarget())
                            {
                                auto dd_payload = ImGui::AcceptDragDropPayload("camera_id");
                                if(dd_payload)
                                {
                                    auto camera_id = string((char*)dd_payload->Data, dd_payload->DataSize);

                                    for(auto c : ui_state.current_revere_update.cameras)
                                    {
                                        if(c.camera_id == camera_id)
                                        {
                                            auto maybe_revere_ip = cfg_state.get_revere_ipv4();
                                            if(!maybe_revere_ip.is_null())
                                            {
                                                auto url_label = c.label;
                                                replace(begin(url_label), end(url_label), ' ', '_');

                                                auto rtsp_url = r_string_utils::format("rtsp://%s:10554/%s", maybe_revere_ip.value().c_str(), url_label.c_str());

                                                stream_info si;
                                                si.name = name;
                                                si.rtsp_url = rtsp_url;
                                                si.camera_id = camera_id;
                                                si.do_motion_detection = c.do_motion_detection;

                                                cfg_state.set_stream_info(si);

                                                ph.update_stream(0, si);

                                                if(cfg_state.get_current_layout() == LAYOUT_ONE_BY_ONE)
                                                {
                                                    reconfigure_control_bar(cfg_state, ph, ui_state);
                                                    ui_state.mcs.obos.cbs.playhead_pos = 1000;
                                                }
                                            }
                                        }
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            if(maybe_rc.is_null())
                            {
                                auto label_size = ImGui::CalcTextSize("Drag camera here...");
                                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(128, 128, 128, 128));
                                ImGui::SetCursorPos(ImVec2((w/2)-(label_size.x/2), (h/2) - (label_size.y/2)));
                                ImGui::Text("Drag camera here...");
                                ImGui::PopStyleColor();
                            }
                        },
                        std::bind(&pipeline_host::control_bar_cb, &ph, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&pipeline_host::control_bar_button_cb, &ph, std::placeholders::_1, std::placeholders::_2),
                        std::bind(&pipeline_host::control_bar_update_data_cb, &ph, std::placeholders::_1),
                        std::bind(&pipeline_host::control_bar_export_cb, &ph, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
                        ph.playing()
                    );
                },
                ui_state.current_revere_update.status_text
            );

            ph.load_video_textures();

            //ImGui::ShowDemoWindow();

            configure_wizard();
            
            if(ui_state.mcs.obos.cbs.exp_state == EXPORT_STATE_FINISHED_SUCCESS)
            {
                _pop_export_folder();
                ui_state.mcs.obos.cbs.exp_state = EXPORT_STATE_NONE;

            }
            else if(ui_state.mcs.obos.cbs.exp_state == EXPORT_STATE_FINISHED_ERROR)
            {
                configure_wizard.next("export_failure");
            }

            ImGui::PopFont();

            tl.work();

            // Rendering
            ImGui::Render();
            int display_w, display_h;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window);

            if(cfg_state.need_save())
                cfg_state.save();
        }

        ph.stop();

        comm_thread.join();
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    r_pipeline::gstreamer_deinit();

    r_utils::r_raw_socket::socket_cleanup();

    return 0;
}
