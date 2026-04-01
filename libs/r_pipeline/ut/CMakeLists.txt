
add_executable(
    r_pipeline_ut
    include/utils.h
    source/utils.cpp
    include/test_r_pipeline.h
    source/test_r_pipeline.cpp
)

target_include_directories(
    r_pipeline_ut PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${CMAKE_CURRENT_SOURCE_DIR}/../../ut_data
)

target_link_libraries(
    r_pipeline_ut LINK_PUBLIC
    experimentum
    r_fakey
    r_pipeline
    r_av
    r_utils
    gstreamer::gstreamer
    ffmpeg::ffmpeg
    opencv::opencv
    platform::platform
)
