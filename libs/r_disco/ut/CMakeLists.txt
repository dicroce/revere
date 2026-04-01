
add_executable(
    r_disco_ut
    include/test_r_disco.h
    source/test_r_disco.cpp
)

target_include_directories(
    r_disco_ut PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_disco_ut LINK_PUBLIC
    experimentum
    r_disco
    r_pipeline
    r_db
    r_onvif
    r_http
    r_utils
    ${GST_LIBS}
)
