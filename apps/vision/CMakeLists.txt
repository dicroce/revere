cmake_minimum_required(VERSION 3.14)
project(vision VERSION 0.0.1)

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib;@executable_path")
    set(CMAKE_BUILD_RPATH "@executable_path/../lib;@executable_path/../../libs/r_utils;@executable_path/../../libs/r_ui_utils;@executable_path/../../libs/r_av;@executable_path/../../libs/r_pipeline;@executable_path/../../libs/r_storage;@executable_path/../../libs/r_motion;@executable_path/../../libs/r_onvif;@executable_path/../../libs/r_http;@executable_path/../../libs/r_db")
else()
    set(CMAKE_INSTALL_RPATH $ORIGIN)
endif()

find_package(OpenGL)

file(GLOB VISION_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

file(GLOB VISION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_executable(
        vision WIN32
        ${VISION_HEADERS}
        ${VISION_SOURCES}
        vision.rc
    )
else()
    add_executable(
        vision
        ${VISION_HEADERS}
        ${VISION_SOURCES}
    )
endif()

#target_compile_definitions(
#    vision PUBLIC
#    USE_STD_FILESYSTEM
#)

target_include_directories(
    vision PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
 
target_link_libraries(
    vision
    r_ui_utils
    r_av
    r_pipeline
    r_db
    r_http
    r_utils
    r_vss
    imgui
    glfw
    gstreamer::gstreamer
    ffmpeg::ffmpeg
    ${OPENGL_LIBRARIES}
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(vision PUBLIC -rdynamic -ggdb3 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fasynchronous-unwind-tables -Wl,--whole-archive)
    endif()
endif()

