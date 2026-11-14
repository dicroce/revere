cmake_minimum_required(VERSION 3.14)
project(r_vss_ut)

add_executable(
    r_vss_ut
    include/test_r_motion_engine.h
    source/test_r_motion_engine.cpp
    include/test_r_camera_stats.h
    source/test_r_camera_stats.cpp
    include/test_r_build_info.h
    source/test_r_build_info.cpp
    include/test_r_snapshot_cache.h
    source/test_r_snapshot_cache.cpp
    include/test_r_frame_tap.h
    source/test_r_frame_tap.cpp
    include/test_r_frame_feeds.h
    source/test_r_frame_feeds.cpp
    include/test_r_live_fanout.h
    source/test_r_live_fanout.cpp
    include/test_r_restream_track.h
    source/test_r_restream_track.cpp
    include/test_r_detection_planner.h
    source/test_r_detection_planner.cpp
    include/test_r_tier_controller.h
    source/test_r_tier_controller.cpp
    include/test_r_tier_outputs.h
    source/test_r_tier_outputs.cpp
    include/test_r_motion_plugin_abi.h
    source/test_r_motion_plugin_abi.cpp
    include/test_r_early_alerts.h
    source/test_r_early_alerts.cpp
    include/test_r_recap.h
    source/test_r_recap.cpp
    include/test_r_event_timing.h
    source/test_r_event_timing.cpp
    include/test_r_detection_cap.h
    source/test_r_detection_cap.cpp
    include/test_r_recording_context.h
    source/test_r_recording_context.cpp
    include/test_r_ptz_session.h
    source/test_r_ptz_session.cpp
    include/test_r_rtsp_transport.h
    source/test_r_rtsp_transport.cpp
    include/test_r_restart_backoff.h
    source/test_r_restart_backoff.cpp
    include/test_r_network_watch.h
    source/test_r_network_watch.cpp
    include/test_r_lag_watchdog.h
    source/test_r_lag_watchdog.cpp
    include/test_r_key_frame_reply.h
    source/test_r_key_frame_reply.cpp
    include/test_r_failure_reason.h
    source/test_r_failure_reason.cpp
    include/test_r_transcode_check.h
    source/test_r_transcode_check.cpp
    include/test_r_system_plugin_dirs.h
    source/test_r_system_plugin_dirs.cpp
    include/test_r_plugin_manifest.h
    source/test_r_plugin_manifest.cpp
    include/test_r_plugin_loader.h
    source/test_r_plugin_loader.cpp
    include/test_r_plugin_installer.h
    source/test_r_plugin_installer.cpp
    include/test_r_cloud_install.h
    source/test_r_cloud_install.cpp
    include/test_r_keep_awake.h
    source/test_r_keep_awake.cpp
    include/test_r_substream_setting.h
    source/test_r_substream_setting.cpp
    include/test_r_ws_auth.h
    source/test_r_ws_auth.cpp
    include/test_r_rtsp_auth.h
    source/test_r_rtsp_auth.cpp
    include/test_r_rtsp_logins.h
    source/test_r_rtsp_logins.cpp
    include/rtsp_test_client.h
    include/test_r_storage_ops.h
    source/test_r_storage_ops.cpp
    include/test_r_teardown_queue.h
    source/test_r_teardown_queue.cpp
    include/test_r_restream_opus.h
    source/test_r_restream_opus.cpp
    include/test_r_opus_transcoder.h
    source/test_r_opus_transcoder.cpp
    include/test_r_substream_recorder.h
    source/test_r_substream_recorder.cpp
    include/test_r_system_plugin_action.h
    source/test_r_system_plugin_action.cpp
    include/test_r_system_plugin_frames.h
    source/test_r_system_plugin_frames.cpp
)

# System plugins for test_r_system_plugin_action and test_r_plugin_loader: one with
# system_plugin_action, one without it (and without get_system_plugin_api_version), and
# one for another plugin API (test_plugin/test_action_system_plugin.cpp).
foreach(variant action no_action other_api)
    add_library(test_${variant}_system_plugin SHARED test_plugin/test_action_system_plugin.cpp)
    target_link_libraries(test_${variant}_system_plugin PRIVATE r_utils)
    target_include_directories(test_${variant}_system_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    target_compile_definitions(test_${variant}_system_plugin PRIVATE TEST_PLUGIN_GUID="com.revere.test.${variant}")
    set_target_properties(test_${variant}_system_plugin PROPERTIES PREFIX "")
    add_dependencies(r_vss_ut test_${variant}_system_plugin)
endforeach()
target_compile_definitions(test_no_action_system_plugin PRIVATE NO_ACTION_EXPORT NO_API_EXPORT)
target_compile_definitions(test_other_api_system_plugin PRIVATE "TEST_PLUGIN_API_VERSION=(R_SYSTEM_PLUGIN_API_VERSION + 1)" RECORD_LOAD)

# A system plugin for test_r_system_plugin_frames: it reads frame feeds through the host's functions.
add_library(test_frames_system_plugin SHARED test_plugin/test_frames_system_plugin.cpp)
target_link_libraries(test_frames_system_plugin PRIVATE r_utils)
target_include_directories(test_frames_system_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
set_target_properties(test_frames_system_plugin PROPERTIES PREFIX "")
add_dependencies(r_vss_ut test_frames_system_plugin)
target_compile_definitions(r_vss_ut PRIVATE
    TEST_ACTION_PLUGIN="$<TARGET_FILE:test_action_system_plugin>"
    TEST_NO_ACTION_PLUGIN="$<TARGET_FILE:test_no_action_system_plugin>"
    TEST_OTHER_API_PLUGIN="$<TARGET_FILE:test_other_api_system_plugin>"
    TEST_FRAMES_PLUGIN="$<TARGET_FILE:test_frames_system_plugin>"
    # test_r_recording_context reads the video callback's source for its wiring.
    R_VSS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../source"
)

# What r_vss/r_build_info.h should report, from the root CMakeLists.
target_compile_definitions(r_vss_ut PRIVATE
    EXPECTED_VERSION="${REVERE_VERSION_STRING}"
    EXPECTED_BUILD_NUMBER=${REVERE_BUILD_NUMBER}
    EXPECTED_GIT_REVISION="${REVERE_GIT_REVISION}"
)

target_include_directories(
    r_vss_ut PUBLIC
    include
    ../include
    ../../ut_data
)

target_link_libraries(
    r_vss_ut LINK_PUBLIC
    experimentum
    r_vss
    r_fakey
    r_pipeline
    r_av
    r_motion
    r_disco
    r_db
    r_onvif
    r_storage
    r_utils
    opencv::opencv
    gstreamer::gstreamer
    ffmpeg::ffmpeg
    platform::platform
)

# test_r_keep_awake drives the Linux session inhibitor over a private D-Bus.
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_search_module(GIO REQUIRED gio-2.0)
    target_include_directories(r_vss_ut PRIVATE ${GIO_INCLUDE_DIRS})
    target_link_directories(r_vss_ut PRIVATE ${GIO_LIBRARY_DIRS})
    target_link_libraries(r_vss_ut LINK_PUBLIC ${GIO_LIBRARIES})
endif()
