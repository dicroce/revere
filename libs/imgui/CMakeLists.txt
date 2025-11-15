cmake_minimum_required(VERSION 3.14)
project(imgui VERSION 0.0.1)

FetchContent_Declare(
  glfw3
  GIT_REPOSITORY https://github.com/glfw/glfw
  GIT_TAG 3.4
)
set(GLFW_INSTALL OFF CACHE BOOL "Disable GLFW install target")
FetchContent_MakeAvailable(glfw3)

find_package(OpenGL REQUIRED)

file(GLOB R_IMGUI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_imgui/*.h
)

file(GLOB R_IMGUI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    imgui STATIC
    ${R_IMGUI_HEADERS}
    ${R_IMGUI_SOURCES}
)

target_compile_definitions(
    imgui PUBLIC
    USE_STD_FILESYSTEM
)

# No need for IMGUI_API_EXPORT or visibility settings for static library

target_include_directories(
    imgui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/imgui>
    $<INSTALL_INTERFACE:include>
    $<INSTALL_INTERFACE:include/imgui>
)

target_link_libraries(
    imgui
    glfw
    ${OPENGL_LIBRARIES}
)

# Install the static library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS imgui
        ARCHIVE DESTINATION revere
    )
endif()
