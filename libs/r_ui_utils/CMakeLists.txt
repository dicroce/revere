cmake_minimum_required(VERSION 3.14)
project(r_ui_utils)

# SDL2 dependency (already found by imgui, but ensure it's available)
include(FindPkgConfig)
pkg_search_module(SDL2 REQUIRED sdl2)

file(GLOB R_UI_UTILS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_ui_utils/*.h
)

file(GLOB R_UI_UTILS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_ui_utils SHARED
    ${R_UI_UTILS_HEADERS}
    ${R_UI_UTILS_SOURCES}
)

target_include_directories(
    r_ui_utils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${SDL2_INCLUDE_DIRS}
)

target_compile_definitions(
    r_ui_utils PRIVATE
    R_UI_UTILS_BUILDING_DLL
)

target_link_libraries(
    r_ui_utils PUBLIC
    imgui
    r_utils
    ${SDL2_LIBRARIES}
)

target_link_directories(
    r_ui_utils PUBLIC
    ${SDL2_LIBRARY_DIRS}
)

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_ui_utils
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()
