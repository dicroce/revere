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
    include/test_r_restream_track.h
    source/test_r_restream_track.cpp
    include/test_r_onvif_event_tracker.h
    source/test_r_onvif_event_tracker.cpp
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
    r_pipeline
    r_av
    r_motion
    r_disco
    r_onvif
    r_storage
    r_utils
    opencv::opencv
    gstreamer::gstreamer
    ffmpeg::ffmpeg
    platform::platform
)
