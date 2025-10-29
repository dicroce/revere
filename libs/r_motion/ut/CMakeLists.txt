cmake_minimum_required(VERSION 3.14)
project(r_motion_ut VERSION 0.0.1)

add_executable(
    r_motion_ut
    include/framework.h
    source/framework.cpp
    include/test_r_motion.h
    source/test_r_motion.cpp
)

target_include_directories(
    r_motion_ut PUBLIC 
    include
    ../include
)

target_link_libraries(
    r_motion_ut LINK_PUBLIC
    r_motion
    r_av
    r_utils
    opencv::opencv
    gstreamer::gstreamer
    ffmpeg::ffmpeg
    platform::platform
)
