cmake_minimum_required(VERSION 3.14)
project(r_vss)

file(GLOB R_VSS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_vss/*.h
)

file(GLOB R_VSS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_vss SHARED
    ${R_VSS_HEADERS}
    ${R_VSS_SOURCES}
)

target_include_directories(
    r_vss PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_vss PUBLIC
    r_pipeline
    r_av
    r_motion
    r_disco
    r_storage
    r_utils
    ffmpeg::ffmpeg
    opencv::opencv
    gstreamer::gstreamer
)

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_vss
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()

add_subdirectory(motion_plugins)
