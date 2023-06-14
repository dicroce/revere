cmake_minimum_required(VERSION 3.14)
project(vision)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_INSTALL_RPATH $ORIGIN)

include(FetchContent)

set(
    SOURCE_FILES
    include/sample.h
    include/pipeline_state.h
    include/stream_info.h
    include/render_context.h
    include/frame.h
    include/imgui_ui.h
    include/timerange.h
    include/control_bar.h

    include/motion_event.h
    source/motion_event.cpp
    include/segment.h
    source/segment.cpp
    include/timerange.h
    source/timerange.cpp
    include/gl_utils.h
    source/gl_utils.cpp
    include/utils.h
    source/utils.cpp
    include/configure_state.h
    source/configure_state.cpp
    include/query.h
    source/query.cpp
    include/layouts.h
    source/layouts.cpp
    include/pipeline_host.h
    source/pipeline_host.cpp
    include/pipeline_state.h
    source/pipeline_state.cpp
    source/main.cpp
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_executable(
        vision WIN32
        ${SOURCE_FILES}
        vision.rc
    )
else()
    add_executable(
        vision
        ${SOURCE_FILES}
    )
endif()

target_compile_definitions(
    vision PUBLIC
    USE_STD_FILESYSTEM
)

target_include_directories(
    vision PUBLIC
    include
    ../r_ui_utils/include
    ../r_codec/include
    ../r_pipeline/include
    ../r_db/include
    ../r_http/include
    ../r_utils/include
    ../imgui/include
    ${GSTREAMER_INCLUDE_DIRS}
)

find_package(OpenGL)

target_link_directories(
    vision PUBLIC
    ${GST_LIBRARY_DIRS}
)

target_link_libraries(
    vision
    r_ui_utils
    r_codec
    r_pipeline
    r_db
    r_http
    r_utils
    imgui
    glfw
    gstcodecparsers-1.0
    ${GST_LIBRARIES}
    ${OPENGL_LIBRARIES}
)
