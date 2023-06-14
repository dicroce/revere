cmake_minimum_required(VERSION 3.14)
project(revere)

set(CMAKE_CXX_STANDARD 17)
 
set(CMAKE_INSTALL_RPATH $ORIGIN)

include(FetchContent)

FetchContent_Declare(
  tray
  GIT_REPOSITORY https://github.com/dicroce/traypp
  GIT_TAG master
)

FetchContent_MakeAvailable(tray)

set(
    SOURCE_FILES 
    include/assignment_state.h
    include/rtsp_source_camera_config.h
    include/imgui_ui.h
    include/gl_utils.h
    source/gl_utils.cpp
    include/utils.h
    source/utils.cpp
    source/main.cpp
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_executable(
        revere WIN32
        ${SOURCE_FILES}
        revere.rc
    )
else()
    add_executable(
        revere
        ${SOURCE_FILES}
    )
endif()

target_compile_definitions(
    revere PUBLIC
    USE_STD_FILESYSTEM
)

target_include_directories(
    revere PUBLIC
    include
    ../r_ui_utils/include
    ../r_motion/include
    ../r_codec/include
    ../r_vss/include
    ../r_disco/include
    ../r_storage/include
    ../r_mux/include
    ../r_fakey/include
    ../r_pipeline/include
    ../r_db/include
    ../r_http/include
    ../r_utils/include
    ../imgui/include
    ${FFMPEG_INCLUDE_DIRS}
)

find_package(OpenGL)

target_link_directories(
    revere PUBLIC
    ${GST_LIBRARY_DIRS}
    ${FFMPEG_LIB_DIRS}
)

target_link_libraries(
    revere
    r_ui_utils
    r_motion
    r_codec
    r_disco
    r_storage
    r_mux
    r_fakey
    r_vss
    r_pipeline
    r_db
    r_http
    r_utils
    imgui
    glfw
    gstcodecparsers-1.0
    ${GST_LIBRARIES}
    ${OPENGL_LIBRARIES}
    tray
)
