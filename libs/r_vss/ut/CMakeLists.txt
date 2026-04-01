cmake_minimum_required(VERSION 3.14)
project(r_vss_ut)

add_executable(
    r_vss_ut
    include/framework.h
    source/framework.cpp
    include/test_r_motion_engine.h
    source/test_r_motion_engine.cpp
)

target_include_directories(
    r_vss_ut PUBLIC
    include
    ../include
    ../../ut_data
)

target_link_libraries(
    r_vss_ut LINK_PUBLIC
    r_vss
    r_pipeline
    r_av
    r_motion
    r_disco
    r_storage
    r_utils
    opencv::opencv
    gstreamer::gstreamer
    ffmpeg::ffmpeg
    platform::platform
)
