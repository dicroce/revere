cmake_minimum_required(VERSION 3.14)
project(r_av VERSION 0.0.1)

file(GLOB R_AV_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_av/*.h
)

file(GLOB R_AV_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_av SHARED
    ${R_AV_HEADERS}
    ${R_AV_SOURCES}
)

target_include_directories(
    r_av PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_av PUBLIC
    r_utils
    ffmpeg::ffmpeg
)

# Install the shared library
install(TARGETS r_av
    RUNTIME DESTINATION revere
    LIBRARY DESTINATION revere
    ARCHIVE DESTINATION revere
)

add_subdirectory(ut)
