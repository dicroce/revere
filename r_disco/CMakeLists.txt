
add_library(
    r_disco
    include/r_disco/r_agent.h
    source/r_agent.cpp
    include/r_disco/r_devices.h
    source/r_devices.cpp
    include/r_disco/r_stream_config.h
    include/r_disco/r_camera.h
    source/r_stream_config.cpp
    include/r_disco/providers/r_onvif_provider.h
    source/providers/r_onvif_provider.cpp
)

target_include_directories(
    r_disco PUBLIC
    include
    ../r_pipeline/include
    ../r_utils/include
    ../r_db/include
    ../r_onvif/include
)

target_link_libraries(
    r_disco
    r_pipeline
    r_db
    r_onvif
    r_utils
)
