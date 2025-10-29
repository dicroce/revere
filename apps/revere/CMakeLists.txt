cmake_minimum_required(VERSION 3.14)
project(revere VERSION 0.0.1)

include(FetchContent)
 
set(CMAKE_INSTALL_RPATH $ORIGIN)

FetchContent_Declare(
  traypp
  GIT_REPOSITORY https://github.com/dicroce/traypp
  GIT_TAG master
)

FetchContent_MakeAvailable(traypp)

FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.15
)

set(PUGIXML_BUILD_SHARED OFF CACHE BOOL "")
set(PUGIXML_INSTALL OFF CACHE BOOL "Disable pugixml install target")

FetchContent_MakeAvailable(pugixml)

find_package(OpenGL)

file(GLOB REVERE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

file(GLOB REVERE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_executable(
        revere WIN32
        ${REVERE_HEADERS}
        ${REVERE_SOURCES}
        revere.rc
    )
else()
    add_executable(
        revere
        ${REVERE_HEADERS}
        ${REVERE_SOURCES}
    )
endif()

#target_compile_definitions(
#    revere PUBLIC
#    USE_STD_FILESYSTEM
#)

target_include_directories(
    revere PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
 
target_link_libraries(
    revere
    r_ui_utils
    r_disco
    r_onvif
    r_storage
    r_fakey
    r_vss
    r_motion
    r_av
    r_pipeline
    r_db
    r_http
    r_utils
    imgui
    pugixml
    glfw
    opencv::opencv
    gstreamer::gstreamer
    ffmpeg::ffmpeg
    ${OPENGL_LIBRARIES}
    tray
    uuid::uuid
    platform::platform
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(revere PUBLIC -rdynamic -ggdb3 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fasynchronous-unwind-tables -Wl,--whole-archive)
    endif()
endif()

