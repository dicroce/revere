cmake_minimum_required(VERSION 3.14)
project(r_fakey)

file(GLOB R_FAKEY_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_fakey/*.h
)

file(GLOB R_FAKEY_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_fakey SHARED
    ${R_FAKEY_HEADERS}
    ${R_FAKEY_SOURCES}
)

target_include_directories(
    r_fakey PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_fakey PUBLIC
    r_utils
    r_av
    ffmpeg::ffmpeg
    gstreamer::gstreamer
)

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_fakey
        RUNTIME DESTINATION revere
        LIBRARY DESTINATION revere
        ARCHIVE DESTINATION revere
    )
endif()

