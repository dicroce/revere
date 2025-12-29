
#include "r_utils/r_file.h"

#ifdef IS_WINDOWS
#include <windows.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

#include <tray.hpp>

#include <string>
#include <thread>
#include <chrono>

#include "r_utils/r_string_utils.h"
#include "r_utils/r_logger.h"
#include "r_utils/r_uuid.h"
#include "r_utils/r_socket.h"
#include "r_utils/r_process.h"
#include "r_utils/r_args.h"
#include "r_utils/r_time_utils.h"
#include "r_disco/r_agent.h"
#include "r_disco/r_devices.h"
#include "r_disco/r_camera.h"
#include "r_vss/r_stream_keeper.h"
#include "r_vss/r_disk_benchmark.h"
#include "r_utils/r_nullable.h"
#include "r_utils/r_file.h"
#include "r_storage/r_md_storage_file.h"
#include "r_pipeline/r_stream_info.h"
#include "r_pipeline/r_gst_source.h"
#include "r_av/r_video_decoder.h"
#include "r_ui_utils/stb_image.h"
#include "r_ui_utils/font_catalog.h"
#include "r_ui_utils/texture_loader.h"
#include "r_ui_utils/wizard.h"

// Define the fonts global variable for this app
std::unordered_map<std::string, r_ui_utils::font_catalog> r_ui_utils::fonts;

#include "utils.h"
#include "gl_utils.h"

#include "imgui_ui.h"
#include "assignment_state.h"
#include "rtsp_source_camera_config.h"

#include "R_32x32.h"
// R_32x32_png && R_32x32_png_len

using namespace std;
using namespace r_utils;
using namespace revere;

struct revere_ui_state
{
    int recording_selected_item {-1};
    int discovered_selected_item {-1};
    vector<revere::sidebar_list_ui_item> recording_items;
    vector<revere::sidebar_list_ui_item> discovered_items;
    string friendly_name;
    string ipv4;
    string restream_url;
    string kbps;
    string retention;

    // camera properties dialog
    bool do_motion_detection {false};
    bool do_motion_pruning {false};
    std::string min_continuous_recording_hours {"24"};

    // camera removal dialog
    bool delete_camera_files {false};
    std::string camera_to_remove_name;

    bool minimize_requested {false};

    // Cached values for performance optimization
    float recording_largest_label {-1.0f};  // -1 means needs recalculation
    float discovered_largest_label {-1.0f}; // -1 means needs recalculation
    void reset_selection()
    {
        recording_selected_item = -1;
        discovered_selected_item = -1;
        friendly_name.clear();
        ipv4.clear();
        restream_url.clear();
        kbps.clear();
        retention.clear();
    }
    bool recording_selected() const {return (recording_selected_item != -1)?true:false;}
    bool discovered_selected() const {return (discovered_selected_item != -1)?true:false;}
    r_nullable<string> selected_camera_id() const
    {
        r_nullable<string> output;
        if(recording_selected())
        {
            if(recording_selected_item != -1)
                output.set_value(recording_items[recording_selected_item].camera_id);
        }
        else
        {
            if(discovered_selected_item != -1)
                output.set_value(discovered_items[discovered_selected_item].camera_id);
        }
        return output;
    }
};

static void glfw_error_callback(int error, const char* description)
{
    R_LOG_ERROR("GLFW Error %d: %s", error, description);
    printf("GLFW Error %d: %s\n", error, description);
}

extern ImGuiContext *GImGui;

void _assign_camera(revere::assignment_state& as, r_disco::r_devices& devices)
{
    auto c = as.camera.value();

    c.rtsp_username = as.rtsp_username;
    c.rtsp_password = as.rtsp_password;
    c.friendly_name = as.camera_friendly_name;
    c.record_file_path = as.file_name;
    if(!as.num_storage_file_blocks.is_null())
        c.n_record_file_blocks = as.num_storage_file_blocks.value();
    if(!as.storage_file_block_size.is_null())
        c.record_file_block_size = as.storage_file_block_size.value();
    c.do_motion_detection.set_value(as.do_motion_detection);
    c.motion_detection_file_path.set_value(as.motion_detection_file_path);

    devices.assign_camera(c);
}

static string _make_file_name(string name)
{
    replace(begin(name), end(name), ' ', '_');
    return name + ".nts";
}

void _update_list_ui(revere_ui_state& ui_state, r_disco::r_devices& devices, r_vss::r_stream_keeper& streamKeeper)
{
    auto assigned_cameras = devices.get_assigned_cameras();

    map<string, r_disco::r_camera> assigned;
    for(auto& c: assigned_cameras)
        assigned[c.id] = c;

    // Get stream status for all cameras to populate kbps and retention
    auto stream_status = streamKeeper.fetch_stream_status();
    map<string, decltype(stream_status)::value_type> status_by_id;
    for(auto& status : stream_status)
        status_by_id[status.camera.id] = status;

    ui_state.recording_items.clear();
    for(auto& c : assigned)
    {
        revere::sidebar_list_ui_item item;
        item.label = c.second.friendly_name.value();
        item.sub_label = c.second.ipv4.value();
        item.camera_id = c.first;

        // Populate kbps and retention if stream status is available
        if(status_by_id.find(c.first) != status_by_id.end())
        {
            auto& status = status_by_id[c.first];
            item.kbps = r_string_utils::format("%ld kbps", (status.bytes_per_second*8)/1024);
            auto retention_days = ((double)streamKeeper.get_retention_hours(status.camera.id).count()) / 24.0;
            item.retention = r_string_utils::format("%.2f days", retention_days);
        }
        else
        {
            item.kbps = "N/A";
            item.retention = "N/A";
        }

        ui_state.recording_items.push_back(item);
    }

    auto all_cameras = devices.get_all_cameras();

    ui_state.discovered_items.clear();
    for(int i = 0; i < (int)all_cameras.size(); ++i)
    {
        auto c = all_cameras[i];

        if(assigned.find(c.id) != assigned.end())
            continue;

        revere::sidebar_list_ui_item item;
        item.label = c.camera_name.value();
        item.sub_label = c.ipv4.value();
        item.camera_id = c.id;
        ui_state.discovered_items.push_back(item);
    }

    // Invalidate cached text size calculations since lists changed
    ui_state.recording_largest_label = -1.0f;
    ui_state.discovered_largest_label = -1.0f;
}

void _create_motion_files(const string& motion_path)
{
    if(r_fs::file_exists(motion_path))
        return;

    r_storage::r_ring::allocate(motion_path, 11, 2592000);

    // Also allocate metadata storage for analytics (person detection, etc.)
    // Use 512KB blocks, 30 blocks total
    r_storage::r_md_storage_file::allocate(motion_path, 524288, 30);
}

void _delete_camera_files(const r_disco::r_camera& camera)
{
    auto video_path = revere::sub_dir("video");

    // Delete the main storage file (.nts)
    if(!camera.record_file_path.is_null())
    {
        auto storage_path = revere::join_path(video_path, camera.record_file_path.value());
        if(r_fs::file_exists(storage_path))
        {
            try
            {
                r_fs::remove_file(storage_path);
                R_LOG_INFO("Deleted camera storage file: %s", storage_path.c_str());
            }
            catch(const std::exception& e)
            {
                R_LOG_ERROR("Failed to delete storage file %s: %s", storage_path.c_str(), e.what());
            }
        }
    }

    // Delete motion detection files (.mdb and .mdnts)
    if(!camera.motion_detection_file_path.is_null())
    {
        auto motion_base_path = revere::join_path(video_path, camera.motion_detection_file_path.value());

        // Delete .mdb file (ring buffer)
        if(r_fs::file_exists(motion_base_path))
        {
            try
            {
                r_fs::remove_file(motion_base_path);
                R_LOG_INFO("Deleted camera motion file: %s", motion_base_path.c_str());
            }
            catch(const std::exception& e)
            {
                R_LOG_ERROR("Failed to delete motion file %s: %s", motion_base_path.c_str(), e.what());
            }
        }

        // Delete .db file (SQLite database for ring buffer)
        auto mdb_db_path = motion_base_path;
        if(mdb_db_path.size() >= 4 && mdb_db_path.substr(mdb_db_path.size() - 4) == ".mdb")
        {
            mdb_db_path = mdb_db_path.substr(0, mdb_db_path.size() - 4) + ".db";
            if(r_fs::file_exists(mdb_db_path))
            {
                try
                {
                    r_fs::remove_file(mdb_db_path);
                    R_LOG_INFO("Deleted camera motion database file: %s", mdb_db_path.c_str());
                }
                catch(const std::exception& e)
                {
                    R_LOG_ERROR("Failed to delete motion database file %s: %s", mdb_db_path.c_str(), e.what());
                }
            }
        }

        // Delete .mdnts file (metadata storage)
        auto mdnts_path = motion_base_path;
        if(mdnts_path.size() >= 4 && mdnts_path.substr(mdnts_path.size() - 4) == ".mdb")
        {
            mdnts_path = mdnts_path.substr(0, mdnts_path.size() - 4) + ".mdnts";
            if(r_fs::file_exists(mdnts_path))
            {
                try
                {
                    r_fs::remove_file(mdnts_path);
                    R_LOG_INFO("Deleted camera metadata file: %s", mdnts_path.c_str());
                }
                catch(const std::exception& e)
                {
                    R_LOG_ERROR("Failed to delete metadata file %s: %s", mdnts_path.c_str(), e.what());
                }
            }

            // Delete .mdnts.db file (SQLite database for metadata storage)
            auto mdnts_db_path = mdnts_path + ".db";
            if(r_fs::file_exists(mdnts_db_path))
            {
                try
                {
                    r_fs::remove_file(mdnts_db_path);
                    R_LOG_INFO("Deleted camera metadata database file: %s", mdnts_db_path.c_str());
                }
                catch(const std::exception& e)
                {
                    R_LOG_ERROR("Failed to delete metadata database file %s: %s", mdnts_db_path.c_str(), e.what());
                }
            }
        }
    }
}

void _on_new_file(revere::assignment_state& as, r_ui_utils::wizard& camera_setup_wizard, r_disco::r_devices& devices, revere_ui_state& ui_state, r_vss::r_stream_keeper& streamKeeper)
{
    auto video_path = revere::sub_dir("video");

    auto c = as.camera.value();

    auto storage_path = revere::join_path(video_path, as.file_name);

    if(r_fs::file_exists(storage_path))
    {
        as.error_message = "Storage file already exists";
        camera_setup_wizard.next("error_modal");
        return;
    }

    uint64_t fs_size = 0;
    uint64_t fs_free = 0;
    r_fs::get_fs_usage(video_path, fs_size, fs_free);

    if(fs_free < (uint64_t)(as.storage_file_block_size.value() * as.num_storage_file_blocks.value()))
    {
        as.error_message = "Not enough free space on storage device.";
        camera_setup_wizard.next("error_modal");
        return;
    }

    if(as.do_motion_detection)
    {
        auto dot_pos = as.file_name.find_last_of('.');
        auto motion_file_name = (dot_pos == string::npos)?as.file_name+".mdb":as.file_name.substr(0, dot_pos)+".mdb";

        auto motion_path = revere::join_path(video_path, motion_file_name);

        _create_motion_files(motion_path);

        as.motion_detection_file_path = motion_file_name;
    }
    
    camera_setup_wizard.cancel();

    r_storage::r_storage_file::allocate(storage_path, as.storage_file_block_size.value(), as.num_storage_file_blocks.value());

    _assign_camera(as, devices);

    _update_list_ui(ui_state, devices, streamKeeper);
}

static AVCodecID _r_encoding_to_avcodec_id(r_pipeline::r_encoding e)
{
    switch(e)
    {
    case r_pipeline::r_encoding::H264_ENCODING:
        return AV_CODEC_ID_H264;
    case r_pipeline::r_encoding::H265_ENCODING:
        return AV_CODEC_ID_HEVC;
    case r_pipeline::r_encoding::AAC_LATM_ENCODING:
        return AV_CODEC_ID_AAC_LATM;
    case r_pipeline::r_encoding::AAC_GENERIC_ENCODING:
        return AV_CODEC_ID_AAC;
    case r_pipeline::r_encoding::PCMU_ENCODING:
        return AV_CODEC_ID_PCM_MULAW;
    case r_pipeline::r_encoding::PCMA_ENCODING:
        return AV_CODEC_ID_PCM_ALAW;
    default:
        R_THROW(("Unknown encoding!"));
    }
}

static void _append_extradata(std::vector<uint8_t>& ed, const std::vector<uint8_t>& start_code, const std::string& base64_data)
{
    auto sprop_buffer = r_string_utils::from_base64(base64_data);
    auto current_size = ed.size();
    ed.resize(current_size + sprop_buffer.size() + start_code.size());
    memcpy(&ed[current_size], &start_code[0], start_code.size());
    memcpy(&ed[current_size + start_code.size()], sprop_buffer.data(), sprop_buffer.size());
}

static r_nullable<shared_ptr<vector<uint8_t>>> _decode_frame(const r_pipeline::sample_context& sample_ctx, const vector<uint8_t>& key_frame, uint16_t output_width, uint16_t output_height, AVPixelFormat fmt)
{
    auto video_enc = sample_ctx.video_encoding();
    if(video_enc.is_null())
        return r_nullable<shared_ptr<vector<uint8_t>>>();

    // Enable parsing to properly handle Annex B streams with multiple NAL units
    r_av::r_video_decoder decoder(_r_encoding_to_avcodec_id(video_enc.value()), true);

    std::vector<uint8_t> ed;
    std::vector<uint8_t> start_code = {0x00, 0x00, 0x00, 0x01};

    auto vps = sample_ctx.sprop_vps();
    auto sps = sample_ctx.sprop_sps();
    auto pps = sample_ctx.sprop_pps();

    if(!vps.is_null() && !vps.value().empty())
        _append_extradata(ed, start_code, vps.value());
    if(!sps.is_null() && !sps.value().empty())
        _append_extradata(ed, start_code, sps.value());
    if(!pps.is_null() && !pps.value().empty())
        _append_extradata(ed, start_code, pps.value());

    // Check if keyframe has inline SPS (NAL type 7)
    bool has_inline_sps = false;
    for(size_t i = 0; i + 4 < key_frame.size() && i < 500; ++i)
    {
        if(key_frame[i] == 0x00 && key_frame[i+1] == 0x00 &&
           key_frame[i+2] == 0x00 && key_frame[i+3] == 0x01)
        {
            uint8_t nal_type = key_frame[i+4] & 0x1F;
            if(nal_type == 7) // SPS
            {
                has_inline_sps = true;
                break;
            }
        }
    }

    // Only set extradata if stream doesn't have inline SPS/PPS
    // When parsing is enabled and stream has inline params, let the parser handle them
    if(ed.size() > 0 && !has_inline_sps)
        decoder.set_extradata(ed);

    decoder.attach_buffer(&key_frame[0], key_frame.size());

    int attempt = 0;
    r_av::r_codec_state state = r_av::R_CODEC_STATE_INITIALIZED;
    while(attempt < 10 && state != r_av::R_CODEC_STATE_HAS_OUTPUT && state != r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT)
    {
        state = decoder.decode();
        ++attempt;
    }

    // If decoder accepted data but hasn't produced output yet, flush to force output
    if(state == r_av::R_CODEC_STATE_HUNGRY)
    {
        while(attempt < 20 && state != r_av::R_CODEC_STATE_HAS_OUTPUT && state != r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT)
        {
            state = decoder.flush();
            ++attempt;
        }
    }

    r_nullable<shared_ptr<vector<uint8_t>>> output;
    if(state == r_av::R_CODEC_STATE_HAS_OUTPUT || state == r_av::R_CODEC_STATE_AGAIN_HAS_OUTPUT)
        output.set_value(decoder.get(fmt, output_width, output_height, 1));

    return output;
}

void configure_camera_setup_wizard(
    revere::assignment_state& as,
    revere::rtsp_source_camera_config& rscc,
    r_ui_utils::wizard& camera_setup_wizard,
    r_ui_utils::texture_loader& tl,
    r_disco::r_agent& agent,
    r_disco::r_devices& devices,
    r_vss::r_stream_keeper& stream_keeper,
    revere_ui_state& ui_state,
    GLFWwindow*
)
{
    camera_setup_wizard.add_step(
        "error_modal",
        [&as, &camera_setup_wizard](){
            ImGui::OpenPopup("Error");
            revere::error_modal(
                GImGui,
                "Error",
                as.error_message.value(),
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "camera_credentials",
        [&](){
            ImGui::OpenPopup("Camera Credentials");
            revere::rtsp_credentials_modal(
                GImGui,
                "Camera Credentials",
                as.rtsp_username,
                as.rtsp_password,
                [&]() {
                    // First, unselect the discovered camera so that when we move it to recording
                    // we're not left with an index pointing at a UI list element that no longer exists.
                    ui_state.discovered_selected_item = -1;

                    try
                    {
                        auto camera = as.camera.value();
                        if(camera.rtsp_url.is_null())
                        {
                            agent.interrogate_camera(
                                camera.camera_name.value(),
                                camera.ipv4.value(),
                                camera.xaddrs.value(),
                                camera.address.value(),
                                as.rtsp_username,
                                as.rtsp_password
                            );
                        }
                        as.camera = devices.get_camera_by_id(as.camera_id);

                        std::thread th([&](){
                            try
                            {
                                auto cp = r_pipeline::fetch_camera_params(as.camera.value().rtsp_url.value(), as.rtsp_username, as.rtsp_password);

                                if(cp.sdp_medias.empty() || cp.bytes_per_second == 0)
                                    R_THROW(("Unable to communicate with camera."));

                                as.byte_rate = cp.bytes_per_second;
                                as.sdp_medias = cp.sdp_medias;    
                                as.maybe_key_frame = _decode_frame(cp.sample_ctx, cp.video_key_frame, 320, 240, AV_PIX_FMT_RGB24);

                                if(as.key_frame_texture != 0)
                                {
                                    tl.destroy_texture(as.key_frame_texture);
                                    as.key_frame_texture = 0;
                                }

                                as.key_frame_texture = tl.create_texture();
                                if(!as.maybe_key_frame.is_null())
                                {
                                    tl.load_texture_from_rgb_memory(
                                        as.key_frame_texture,
                                        as.maybe_key_frame.raw()->data(),
                                        as.maybe_key_frame.raw()->size(),
                                        320,
                                        240
                                    );
                                }

                                if(camera_setup_wizard.active())
                                    camera_setup_wizard.next("friendly_name");
                            }
                            catch(const r_utils::r_unauthorized_exception&)
                            {
                                as.error_message = "Invalid Credentials";
                                camera_setup_wizard.next("error_modal");
                            }
                            catch(const std::exception& e)
                            {
                                as.error_message = e.what();
                                camera_setup_wizard.next("error_modal");
                            }
                        });
                        th.detach();

                        camera_setup_wizard.next("please_wait");
                    }
                    catch(const r_utils::r_unauthorized_exception&)
                    {
                        as.error_message = "Invalid Credentials";
                        camera_setup_wizard.next("error_modal");
                    }
                    catch(const std::exception& e)
                    {
                        as.error_message = e.what();
                        camera_setup_wizard.next("error_modal");
                    }
                },
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "friendly_name",
        [&as, &camera_setup_wizard](){
            ImGui::OpenPopup("Camera Friendly Name");
            revere::friendly_name_modal(
                GImGui,
                "Camera Friendly Name",
                as,
                as.camera_friendly_name,
                [&]() {
                    camera_setup_wizard.next("motion_detection");
                },
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "please_wait",
        [&as, &camera_setup_wizard](){
            ImGui::OpenPopup("Please Wait");
            revere::please_wait_modal(
                GImGui,
                "Please Wait",
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "motion_detection",
        [&as, &camera_setup_wizard](){
            ImGui::OpenPopup("Motion Detection");
            revere::motion_detection_modal(
                GImGui,
                "Motion Detection",
                as.do_motion_detection,
                [&](){camera_setup_wizard.next("new_or_existing");},
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "new_or_existing",
        [&as, &camera_setup_wizard, &devices, &ui_state](){
            ImGui::OpenPopup("New or Existing storage file?");
            revere::new_or_existing_modal(
                GImGui,
                "New or Existing storage file?",
                [&](){camera_setup_wizard.next("retention");},
                [&](){camera_setup_wizard.next("choose_file");},
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "choose_file",
        [&as, &camera_setup_wizard, &devices, &ui_state, &stream_keeper](){
            ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".nts", revere::sub_dir("video"));
            if(ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey", ImGuiWindowFlags_None, ImVec2(800, 600))) 
            {
                if(ImGuiFileDialog::Instance()->IsOk())
                {
                    auto currentPath = ImGuiFileDialog::Instance()->GetCurrentPath();
                    std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();
                    auto fileName = filePathName.substr(currentPath.size() + 1);
                    as.file_name = fileName;

                    auto dotPos = fileName.find_last_of('.');
                    auto mdbFileName = fileName.substr(0, dotPos) + ".mdb";
                    auto mdbPathName = revere::join_path(ImGuiFileDialog::Instance()->GetCurrentPath(), mdbFileName);
                    if(r_fs::file_exists(mdbPathName))
                    {
                        as.do_motion_detection = true;
                        as.motion_detection_file_path = mdbFileName;
                    }

                    _assign_camera(as, devices);

                    ui_state.reset_selection();

                    _update_list_ui(ui_state, devices, stream_keeper);

                    camera_setup_wizard.cancel();
                }
                else camera_setup_wizard.cancel();
                
                ImGuiFileDialog::Instance()->Close();
            }
        }
    );
    camera_setup_wizard.add_step(
        "retention",
        [&as, &camera_setup_wizard](){
            ImGui::OpenPopup("Configure Retention");
            revere::retention_modal(
                GImGui,
                "Configure Retention",
                as,
                [&](){camera_setup_wizard.next("new_file_name");},
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "new_file_name",
        [&as, &camera_setup_wizard, &devices, &ui_state, &stream_keeper](){
            as.file_name = _make_file_name(as.camera_friendly_name);
            ImGui::OpenPopup("New File Name");
            revere::new_file_name_modal(
                GImGui,
                "New File Name",
                as.file_name,
                [&](){_on_new_file(as, camera_setup_wizard, devices, ui_state, stream_keeper);},
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );
    camera_setup_wizard.add_step(
        "minimize_to_tray",
        [&ui_state, &camera_setup_wizard](){
            ImGui::OpenPopup("Minimize to Tray");
            revere::minimize_to_tray_modal(
                GImGui,
                "Minimize to Tray",
                [&](){camera_setup_wizard.cancel(); ui_state.minimize_requested = true;},
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );

    camera_setup_wizard.add_step(
        "configure_rtsp_source_camera",
        [&ui_state, &camera_setup_wizard, &rscc, &devices](){
            ImGui::OpenPopup("Configure RTSP Source Camera");
            revere::configure_rtsp_source_camera_modal(
                GImGui,
                "Configure RTSP Source Camera",
                rscc,
                [&](){
                    r_disco::r_camera c;
                    c.id = r_uuid::generate();
                    c.camera_name.set_value(rscc.camera_name);
                    c.ipv4.set_value(rscc.ipv4);
                    c.rtsp_url.set_value(rscc.rtsp_url);
                    if(rscc.rtsp_username.size() > 0)
                    {
                        c.rtsp_username.set_value(rscc.rtsp_username);
                        c.rtsp_password.set_value(rscc.rtsp_password);
                    }

                    devices.save_camera(c);

                    camera_setup_wizard.cancel();
                },
                [&](){camera_setup_wizard.cancel();}
            );
        }
    );

    camera_setup_wizard.add_step(
        "camera_properties_modal",
        [&ui_state, &camera_setup_wizard, &rscc, &devices, &stream_keeper, &as](){

            ImGui::OpenPopup("Camera Properties");
            revere::camera_properties_modal(
                GImGui,
                "Camera Properties",
                ui_state.do_motion_detection,
                ui_state.do_motion_pruning,
                ui_state.min_continuous_recording_hours,
                [&](){
                    // OK button
                    auto camera_id = ui_state.selected_camera_id();
                    auto camera = devices.get_camera_by_id(camera_id.value()).value();

                    string initial_md_file_name;
                    if(!camera.motion_detection_file_path.is_null())
                        initial_md_file_name = camera.motion_detection_file_path.value();

                    string motion_file_name = initial_md_file_name;
                    auto existing_path = revere::join_path(revere::sub_dir("video"), motion_file_name);
                    if(!r_fs::file_exists(existing_path) || motion_file_name.empty())
                    {
                        motion_file_name = camera.record_file_path.value();

                        auto dot_pos = motion_file_name.find_last_of('.');

                        motion_file_name = (dot_pos == string::npos)?motion_file_name+".mdb":motion_file_name.substr(0, dot_pos)+".mdb";

                        auto motion_path = revere::join_path(revere::sub_dir("video"), motion_file_name);

                        _create_motion_files(motion_path);
                    }

                    camera.do_motion_detection.set_value(ui_state.do_motion_detection);
                    camera.motion_detection_file_path.set_value(motion_file_name);
                    camera.do_motion_pruning.set_value(ui_state.do_motion_pruning);
                    camera.min_continuous_recording_hours.set_value(r_string_utils::s_to_int(ui_state.min_continuous_recording_hours));
                    devices.save_camera(camera);

                    stream_keeper.bounce(camera_id.value());

                    ui_state.do_motion_detection = false;
                    ui_state.do_motion_pruning = false;
                    ui_state.min_continuous_recording_hours = "24";

                    camera_setup_wizard.cancel();
                },
                [&](){
                    ui_state.do_motion_detection = false;
                    ui_state.do_motion_pruning = false;
                    ui_state.min_continuous_recording_hours = "24";

                    camera_setup_wizard.cancel();
                }
            );
        }
    );

    camera_setup_wizard.add_step(
        "remove_camera_modal",
        [&ui_state, &camera_setup_wizard, &devices](){
            ImGui::OpenPopup("Remove Camera");
            revere::remove_camera_modal(
                GImGui,
                "Remove Camera",
                ui_state.camera_to_remove_name,
                ui_state.delete_camera_files,
                [&](){
                    // Delete files callback
                    auto camera_id = ui_state.selected_camera_id();
                    if(!camera_id.is_null())
                    {
                        auto maybe_camera = devices.get_camera_by_id(camera_id.value());
                        if(!maybe_camera.is_null())
                        {
                            auto camera = maybe_camera.value();

                            // First, unassign the camera - this will trigger stream_keeper
                            // to stop recording and close all file handles
                            devices.unassign_camera(camera);

                            // Wait a bit to ensure all file handles are closed
                            // The stream_keeper's main loop polls every 2 seconds, so we need to wait
                            // for at least one iteration to process the unassignment
                            std::this_thread::sleep_for(std::chrono::milliseconds(3000));

                            // Now it's safe to delete the files
                            _delete_camera_files(camera);
                        }
                    }
                    ui_state.reset_selection();
                    ui_state.delete_camera_files = false;
                    camera_setup_wizard.cancel();
                },
                [&](){
                    // Keep files callback
                    auto camera_id = ui_state.selected_camera_id();
                    if(!camera_id.is_null())
                    {
                        auto maybe_camera = devices.get_camera_by_id(camera_id.value());
                        if(!maybe_camera.is_null())
                        {
                            auto camera = maybe_camera.value();
                            devices.unassign_camera(camera);
                        }
                    }
                    ui_state.reset_selection();
                    ui_state.delete_camera_files = false;
                    camera_setup_wizard.cancel();
                },
                [&](){
                    // Cancel callback
                    ui_state.delete_camera_files = false;
                    camera_setup_wizard.cancel();
                }
            );
        }
    );
}

string _get_icon_path()
{
#ifdef IS_WINDOWS
    return r_fs::path_join(
        r_fs::working_directory(),
        "R.ico"
    );
#endif

#if defined(IS_LINUX) || defined(IS_MACOS)
    // Try AppImage-style icon path first
    std::string rel_icon_path = "/../share/icons/hicolor/128x128/apps/revere.png";
    std::string full_icon_path = r_fs::path_join(r_fs::working_directory(), rel_icon_path);

    if (r_fs::file_exists(full_icon_path))
        return full_icon_path;

    // Fallback to local PNG icon
    return r_fs::path_join(r_fs::working_directory(), "R.png");
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

void _set_adaptive_window_size(GLFWwindow* window)
{
    // Get primary monitor and its workarea (which excludes taskbars/menubars)
    GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
    if (primary_monitor == NULL)
    {
        R_LOG_WARNING("Unable to detect primary monitor, using default window size");
        return;
    }

    int monitor_x, monitor_y, monitor_width, monitor_height;
    glfwGetMonitorWorkarea(primary_monitor, &monitor_x, &monitor_y, &monitor_width, &monitor_height);

    R_LOG_INFO("Monitor workarea: %dx%d at (%d, %d)", monitor_width, monitor_height, monitor_x, monitor_y);

    // Target window should be approximately 1/3 of monitor width and 1/3 of monitor height
    // On macOS, use a larger initial window size (75% instead of 1/3)
#ifdef IS_MACOS
    int target_width = (monitor_width * 3) / 4;
    int target_height = (monitor_height * 3) / 4;
#else
    int target_width = monitor_width / 3;
    int target_height = monitor_height / 3;
#endif

    // Ensure minimum usable size (640x360 is reasonable minimum)
    const int min_width = 640;
    const int min_height = 360;
    if (target_width < min_width || target_height < min_height)
    {
        R_LOG_WARNING("Calculated window size too small (%dx%d), using minimum", target_width, target_height);
        target_width = min_width;
        target_height = min_height;
    }

    // Ensure we don't exceed monitor workarea
    if (target_width > monitor_width)
        target_width = monitor_width;
    if (target_height > monitor_height)
        target_height = monitor_height;

    R_LOG_INFO("Setting adaptive window size: %dx%d", target_width, target_height);
    glfwSetWindowSize(window, target_width, target_height);

    // Center window on monitor
    int window_x = monitor_x + (monitor_width - target_width) / 2;
    int window_y = monitor_y + (monitor_height - target_height) / 2;
    glfwSetWindowPos(window, window_x, window_y);
}

void _set_window_icon(GLFWwindow* window)
{
    GLFWimage images[1];
    int x, y, channels;
    images[0].pixels = stbi_load_from_memory(R_32x32_png, R_32x32_png_len, &x, &y, &channels, 4);
    //auto icon_file = r_file::open("R.png", "r");
    //images[0].pixels = stbi_load_from_file(icon_file, &x, &y, &channels, 4);
    images[0].width = x;
    images[0].height = y;

    glfwSetWindowIcon(window, 1, images);
    stbi_image_free(images[0].pixels);
}

#ifdef IS_WINDOWS
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR pCmdLine, int)
#endif
#if defined(IS_LINUX) || defined(IS_MACOS)
int main(int argc, char** argv)
#endif
{
    r_logger::install_terminate();

    auto top_dir = revere::top_dir();
    auto log_path = revere::sub_dir("logs");

    r_logger::install_logger(r_fs::platform_path(log_path), "revere_log_");

    // UI state needs to be created before we can register the log callback
    // We'll register it after creating ui_state

#ifdef IS_WINDOWS
    int argc;
    LPWSTR* argv_p = CommandLineToArgvW(pCmdLine, &argc);
    vector<string> arg_storage;
    for(int i = 0; i < argc; i++)
        arg_storage.push_back(r_string_utils::convert_wide_string_to_multi_byte_string(argv_p[i]));
    argc = (int)arg_storage.size();
    vector<char*> argv(argc);
    for(int i = 0; i < argc; i++)
        argv[i] = const_cast<char*>(arg_storage[i].c_str());
#endif
    auto args = r_args::parse_arguments(argc, &argv[0]);

    auto maybe_start_minimized = (r_args::get_optional_argument(args, "--start_minimized", "false").value() == "false")? false : true;

    r_raw_socket::socket_startup();

    _set_working_dir();

    r_pipeline::gstreamer_init();

    // Setup Dear ImGui context early so we can pass it to stream keeper
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    r_disco::r_agent agent(r_fs::platform_path(top_dir));
    r_disco::r_devices devices(r_fs::platform_path(top_dir));
    r_vss::r_stream_keeper streamKeeper(devices, r_fs::platform_path(top_dir));

    agent.set_stream_change_cb(bind(&r_disco::r_devices::insert_or_update_devices, &devices, placeholders::_1));
    agent.set_credential_cb(bind(&r_disco::r_devices::get_credentials, &devices, placeholders::_1));
    agent.set_is_recording_cb(bind(&r_vss::r_stream_keeper::is_recording, &streamKeeper, placeholders::_1));

    // Note: streamKeeper, devices, and agent will be started after log callback is registered

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
    {
        R_LOG_ERROR("Unable to initialize GLFW");
        return 1;
    }

    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#ifdef IS_MACOS
    // macOS requires at least OpenGL 3.2 with Core Profile
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glsl_version = "#version 150";
#endif

    // Create window with graphics context
    GLFWwindow* window = glfwCreateWindow(960, 540, "Revere", NULL, NULL);
    if (window == NULL)
    {
        R_LOG_ERROR("Unable to glfwCreateWindow");
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Set adaptive window size based on monitor resolution
    _set_adaptive_window_size(window);

    string vision_cmd = "vision";
#ifdef IS_WINDOWS
    vision_cmd = "vision.exe";
#elif defined(IS_MACOS)
    // On macOS, use 'open' command to launch the app bundle
    vision_cmd = "open -n /Applications/Vision.app";
#endif

    r_process vision_process(vision_cmd, true); // Use detached process

#if !defined(IS_MACOS)
    _set_window_icon(window);
#endif

    bool close_requested = false;

    R_LOG_INFO("wd=%s\n",r_fs::working_directory().c_str());

    auto icon_path = _get_icon_path();

    R_LOG_INFO("icon_path=%s\n",icon_path.c_str());

    Tray::Tray tray("Revere", icon_path);
    tray.addEntry(Tray::Button("Exit", [&]{
        close_requested = true;
        tray.exit();
    }));
    tray.addEntry(Tray::Button("Show", [&]{
        glfwShowWindow(window);
    }));
    tray.addEntry(Tray::Button("Hide", [&]{
        glfwHideWindow(window);
    }));
    tray.addEntry(Tray::Button("Launch Vision", [&]{
        if(!vision_process.running())
            vision_process.start();
    }));

    // Configure Dear ImGui (context already created earlier)
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Platform-specific font sizes (macOS renders fonts larger, so we use smaller sizes)
#ifdef IS_MACOS
    const float FONT_SIZE_24 = 18.0f;
    const float FONT_SIZE_22 = 16.0f;
    const float FONT_SIZE_20 = 15.0f;
    const float FONT_SIZE_18 = 14.0f;
    const float FONT_SIZE_14 = 15.0f;
#else
    const float FONT_SIZE_24 = 24.0f;
    const float FONT_SIZE_22 = 22.0f;
    const float FONT_SIZE_20 = 20.0f;
    const float FONT_SIZE_18 = 18.0f;
    const float FONT_SIZE_14 = 16.0f;
#endif

    r_ui_utils::load_fonts(io, FONT_SIZE_24, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, FONT_SIZE_22, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, FONT_SIZE_20, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, FONT_SIZE_18, r_ui_utils::fonts);
    r_ui_utils::load_fonts(io, FONT_SIZE_14, r_ui_utils::fonts);

    // Generate font key strings for the loaded sizes
    auto FONT_KEY_24 = r_string_utils::float_to_s(FONT_SIZE_24, 2);
    auto FONT_KEY_22 = r_string_utils::float_to_s(FONT_SIZE_22, 2);
    auto FONT_KEY_20 = r_string_utils::float_to_s(FONT_SIZE_20, 2);
    auto FONT_KEY_18 = r_string_utils::float_to_s(FONT_SIZE_18, 2);
    auto FONT_KEY_14 = r_string_utils::float_to_s(FONT_SIZE_14, 2);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    revere_ui_state ui_state;

    // Log startup banner
    R_LOG_INFO("========================================");
    R_LOG_INFO("Revere Video Surveillance System");
    R_LOG_INFO("Starting up...");
    R_LOG_INFO("========================================");

    // Benchmark disk write speed before starting recording services
    auto video_path = revere::sub_dir("video");
    uint64_t disk_bytes_per_second = r_vss::benchmark_disk_write_speed(video_path);

    // Start background services
    streamKeeper.start();
    devices.start();
    agent.start();

    r_ui_utils::texture_loader tl;
    revere::assignment_state as;
    revere::rtsp_source_camera_config rscc;
    r_ui_utils::wizard camera_setup_wizard;
    configure_camera_setup_wizard(as, rscc, camera_setup_wizard, tl, agent, devices, streamKeeper, ui_state, window);

    _update_list_ui(ui_state, devices, streamKeeper);

    // Auto-select first camera in Recording list if any exist
    if(ui_state.recording_items.size() > 0)
    {
        ui_state.recording_selected_item = 0;
    }

    auto last_ui_update_ts = chrono::steady_clock::now();
    bool force_ui_update = false;


    // Main loop
    while(!close_requested)
    {        
        auto now = chrono::steady_clock::now();

        if(((now - last_ui_update_ts) > chrono::seconds(5)) || force_ui_update)
        {
            last_ui_update_ts = now;
            force_ui_update = false;

            _update_list_ui(ui_state, devices, streamKeeper);

            // Also update stream status for selected camera (only every 5 seconds, not every frame)
            auto maybe_camera_id = ui_state.selected_camera_id();
            if(!maybe_camera_id.is_null())
            {
                auto stream_status = streamKeeper.fetch_stream_status();

                for(auto status : stream_status)
                {
                    if(status.camera.id == maybe_camera_id.value())
                    {
                        ui_state.friendly_name = status.camera.friendly_name.value();
                        ui_state.ipv4 = status.camera.ipv4.value();
                        ui_state.restream_url = r_string_utils::format("rtsp://127.0.0.1:10554/%s",ui_state.friendly_name.c_str());
                        ui_state.kbps = r_string_utils::format("%ld kbps", (status.bytes_per_second*8)/1024);
                        auto retention_days = ((double)streamKeeper.get_retention_hours(status.camera.id).count()) / 24.0;
                        ui_state.retention = r_string_utils::format("%.2f days", retention_days);
                    }
                }
            }
        }

        glfwPollEvents();

        tray.pump();

        if(maybe_start_minimized)
        {
            maybe_start_minimized = false;
            glfwHideWindow(window);
        }

        if(glfwWindowShouldClose(window) != GL_FALSE)
        {
            glfwSetWindowShouldClose(window, GL_FALSE);
            camera_setup_wizard.next("minimize_to_tray");
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        auto window_size = ImGui::GetIO().DisplaySize;
        auto window_width = (uint16_t)window_size.x;
        auto window_height = (uint16_t)window_size.y;

        ImGui::PushFont(r_ui_utils::fonts[FONT_KEY_24].roboto_regular);

        auto client_top = revere::main_menu(
            [&](){
                close_requested = true;
                tray.exit();
            },
            [&](){
                camera_setup_wizard.next("minimize_to_tray");
            },
            [&](){
                camera_setup_wizard.next("configure_rtsp_source_camera");
            },
            [&](){
                if(!vision_process.running())
                    vision_process.start();
            }
        );

        r_utils::r_nullable<std::string> main_status;
        main_status.set_value(string("Revere Running"));

        main_layout(
            client_top,
            window_width,
            window_height,
            [&](uint16_t x, uint16_t y, uint16_t w, uint16_t h){
                revere::thirds(
                    x,
                    y,
                    w,
                    h,
                    "Discovered",
                    [&](uint16_t pw){
                        revere::sidebar_list(
                            GImGui,
                            as,
                            ui_state.discovered_selected_item,
                            pw,
                            ui_state.discovered_items,
                            "Record",
                            [&](int i){
                                as.camera_id = ui_state.discovered_items[i].camera_id;
                                as.camera = devices.get_camera_by_id(as.camera_id);
                                as.ipv4 = as.camera.value().ipv4.value();

                                camera_setup_wizard.next("camera_credentials");
                            },
                            [&](int i){
                                ui_state.reset_selection();
                                ui_state.discovered_selected_item = i;
                                ui_state.recording_selected_item = -1;
                                force_ui_update = true;
                            },
                            true, // should we include a "forget" button?
                            [&](int i){
                                auto camera_id = ui_state.discovered_items[i].camera_id;
                                auto camera = devices.get_camera_by_id(camera_id);
                                if(!camera.is_null())
                                {
                                    devices.remove_camera(camera.value());
                                    agent.forget(camera_id);
                                    ui_state.reset_selection();
                                    force_ui_update = true;
                                }
                            },
                            false, // Dont include the properites button
                            [](int){},
                            ui_state.discovered_largest_label,
                            FONT_KEY_24,
                            FONT_KEY_22
                        );
                    },
                    "Recording",
                    [&](uint16_t pw){
                        revere::sidebar_list(
                            GImGui,
                            as,
                            ui_state.recording_selected_item,
                            pw,
                            ui_state.recording_items,
                            "Remove",
                            [&](int i){
                                // Set up the UI state for the remove dialog
                                ui_state.recording_selected_item = i;
                                ui_state.discovered_selected_item = -1;
                                force_ui_update = true;

                                auto maybe_c = devices.get_camera_by_id(ui_state.recording_items[i].camera_id);
                                if(!maybe_c.is_null())
                                {
                                    auto c = maybe_c.value();
                                    // Store camera name for display in the dialog
                                    ui_state.camera_to_remove_name = c.friendly_name.is_null() ?
                                        c.camera_name.value() : c.friendly_name.value();
                                    // Reset the delete files checkbox to false (unchecked by default)
                                    ui_state.delete_camera_files = false;
                                    // Show the removal confirmation dialog
                                    camera_setup_wizard.next("remove_camera_modal");
                                }

                                force_ui_update = true;
                            },
                            [&](int i){
                                ui_state.recording_selected_item = i;
                                ui_state.discovered_selected_item = -1;
                                force_ui_update = true;
                            },
                            false, // Dont include forget button
                            [](int){},
                            true, // Do include the properties button
                            [&](int i){
                                // first update the ui state
                                ui_state.recording_selected_item = i;
                                ui_state.discovered_selected_item = -1;
                                force_ui_update = true;

                                // Go to camera properties modal
                                auto camera = devices.get_camera_by_id(ui_state.selected_camera_id().value()).value();
                                ui_state.do_motion_detection = camera.do_motion_detection.value();
                                ui_state.do_motion_pruning = camera.do_motion_pruning.value();
                                ui_state.min_continuous_recording_hours = r_string_utils::int_to_s(camera.min_continuous_recording_hours.value());
                                camera_setup_wizard.next("camera_properties_modal");
                            },
                            ui_state.recording_largest_label,
                            FONT_KEY_24,
                            FONT_KEY_22
                        );
                    },
                    "Loaded System Plugins",
                    [&](uint16_t){
                        ImGui::PushFont(r_ui_utils::fonts[FONT_KEY_18].roboto_regular);

                        auto loaded_plugins = streamKeeper.get_loaded_system_plugins();

                        if (loaded_plugins.empty())
                        {
                            ImGui::Text("No system plugins loaded");
                        }
                        else
                        {
                            ImGui::Text("Loaded plugins:");
                            ImGui::Spacing();
                            for (const auto& plugin_name : loaded_plugins)
                            {
                                ImGui::BulletText("%s", plugin_name.c_str());
                            }
                        }

                        ImGui::PopFont();
                    }
                );

            },
            main_status,
            FONT_KEY_18
        );

        camera_setup_wizard();

        ImGui::PopFont();

        tl.work();

        if(ui_state.minimize_requested)
        {
            ui_state.minimize_requested = false;
            glfwHideWindow(window);
        }

        // Always call Render to complete the frame (required by ImGui)
        ImGui::Render();

        // Only render to screen if window is visible
        if(glfwGetWindowAttrib(window, GLFW_VISIBLE))
        {
            int display_w, display_h;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window);
        }
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    streamKeeper.stop();
    agent.stop();
    // Note: devices.stop() is called by its destructor AFTER streamKeeper is destroyed
    // This order is important because plugins may use devices during their shutdown

    r_pipeline::gstreamer_deinit();

    r_raw_socket::socket_cleanup();

    r_logger::uninstall_logger();

    return 0;
}
