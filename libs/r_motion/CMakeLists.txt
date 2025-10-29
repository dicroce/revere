cmake_minimum_required(VERSION 3.14)
project(r_motion VERSION 0.0.1)

file(GLOB R_MOTION_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_motion/*.h
)

file(GLOB R_MOTION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_motion SHARED
    ${R_MOTION_HEADERS}
    ${R_MOTION_SOURCES}
)

target_include_directories(
    r_motion PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_motion PUBLIC
    r_utils
    opencv::opencv
    gstreamer::gstreamer
    platform::platform
)

# Install the shared library
install(TARGETS r_motion
    RUNTIME DESTINATION revere
    LIBRARY DESTINATION revere
    ARCHIVE DESTINATION revere
)

add_subdirectory(ut)
