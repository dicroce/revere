add_executable(
    r_cameras_ut
    include/test_r_onvif_event_tracker.h
    source/test_r_onvif_event_tracker.cpp
    include/test_r_reolink.h
    source/test_r_reolink.cpp
)

target_include_directories(
    r_cameras_ut PUBLIC
    include
    ../include
)

target_link_libraries(
    r_cameras_ut PRIVATE
    experimentum
    r_cameras
    r_onvif
    r_disco
    r_http
    r_utils
    uuid::uuid
    platform::platform
)
