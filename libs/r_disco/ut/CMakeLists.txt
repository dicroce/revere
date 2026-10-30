
add_executable(
    r_disco_ut
    include/test_r_disco.h
    source/test_r_disco.cpp
)

target_include_directories(
    r_disco_ut PUBLIC
    include
    ../include
)

target_link_libraries(
    r_disco_ut PRIVATE
    experimentum
    r_disco
    r_pipeline
    r_db
    r_onvif
    r_http
    r_utils
    gstreamer::gstreamer
    platform::platform
)
