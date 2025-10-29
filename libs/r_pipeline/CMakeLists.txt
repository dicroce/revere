cmake_minimum_required(VERSION 3.14)
project(r_pipeline VERSION 0.0.1)

file(GLOB R_PIPELINE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_pipeline/*.h
)

file(GLOB R_PIPELINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_pipeline SHARED
    ${R_PIPELINE_HEADERS}
    ${R_PIPELINE_SOURCES}
)

target_include_directories(
    r_pipeline PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_pipeline PUBLIC
    r_utils
    gstreamer::gstreamer
)

# Install the shared library
install(TARGETS r_pipeline
    RUNTIME DESTINATION revere
    LIBRARY DESTINATION revere
    ARCHIVE DESTINATION revere
)

add_subdirectory(ut)
