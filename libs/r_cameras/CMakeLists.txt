cmake_minimum_required(VERSION 3.14)
project(r_cameras)

# What Revere needs from cameras' own detection (motion and object events),
# behind one interface, with a directory per way of getting it (onvif/,
# reolink/, and other vendors' APIs as they come). Shared, like the other
# libraries (plugins must see one copy of anything they share), so it is
# installed below and bundled on macOS (apps/revere and apps/vision).

file(GLOB_RECURSE R_CAMERAS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_cameras/*.h
)

file(GLOB_RECURSE R_CAMERAS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_cameras SHARED
    ${R_CAMERAS_HEADERS}
    ${R_CAMERAS_SOURCES}
)

target_include_directories(
    r_cameras PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_cameras PUBLIC
    r_onvif
    r_disco
    r_http
    r_utils
)

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_cameras
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()

add_subdirectory(ut)
