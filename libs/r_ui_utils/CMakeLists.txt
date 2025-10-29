cmake_minimum_required(VERSION 3.14)
project(r_ui_utils VERSION 0.0.1)

find_package(OpenGL REQUIRED)

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
)

target_compile_definitions(
    r_ui_utils PRIVATE
    R_UI_UTILS_BUILDING_DLL
)

target_link_libraries(
    r_ui_utils PUBLIC
    imgui
    r_utils
    glfw
    ${OPENGL_LIBRARIES}
)

# Install the shared library
install(TARGETS r_ui_utils
    RUNTIME DESTINATION revere
    LIBRARY DESTINATION revere
    ARCHIVE DESTINATION revere
)
