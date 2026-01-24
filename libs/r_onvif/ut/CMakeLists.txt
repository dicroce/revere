
add_executable(
    r_onvif_ut
    include/framework.h
    source/framework.cpp
    include/test_r_onvif.h
    source/test_r_onvif.cpp
)

target_include_directories(
    r_onvif_ut PUBLIC 
    include
    ../include
)

target_include_directories(
    r_onvif_ut PRIVATE
)

target_link_libraries(
    r_onvif_ut PRIVATE
    r_onvif
    r_http
    r_utils
    uuid::uuid
    pugixml::pugixml
    gstreamer::gstreamer
    opencv::opencv
    platform::platform
)
