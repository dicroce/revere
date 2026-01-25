cmake_minimum_required(VERSION 3.14)
project(r_motion)

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

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_motion
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()

add_subdirectory(ut)
