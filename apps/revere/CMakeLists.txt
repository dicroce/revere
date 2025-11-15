cmake_minimum_required(VERSION 3.14)
project(revere VERSION 0.0.1)

include(FetchContent)

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib;@executable_path")
    set(CMAKE_BUILD_RPATH "@executable_path/../lib;@executable_path/../../libs/r_utils;@executable_path/../../libs/r_ui_utils;@executable_path/../../libs/r_av;@executable_path/../../libs/r_pipeline;@executable_path/../../libs/r_storage;@executable_path/../../libs/r_motion;@executable_path/../../libs/r_onvif;@executable_path/../../libs/r_http;@executable_path/../../libs/r_db;@executable_path/../../libs/r_fakey")
else()
    set(CMAKE_INSTALL_RPATH $ORIGIN)
endif()

# System tray support (not yet available on macOS)
if(NOT CMAKE_SYSTEM_NAME MATCHES "Darwin")
    FetchContent_Declare(
      traypp
      GIT_REPOSITORY https://github.com/dicroce/traypp
      GIT_TAG master
    )

    FetchContent_MakeAvailable(traypp)
endif()

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
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_executable(
        revere MACOSX_BUNDLE
        ${REVERE_HEADERS}
        ${REVERE_SOURCES}
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
    uuid::uuid
    platform::platform
)

# Link tray library only on platforms where it's available
if(NOT CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_link_libraries(revere tray)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(revere PUBLIC -rdynamic -ggdb3 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fasynchronous-unwind-tables -Wl,--whole-archive)
    endif()
endif()

# macOS app bundle configuration
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set_target_properties(revere PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.revere.app"
        MACOSX_BUNDLE_BUNDLE_NAME "Revere"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.0.1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.0.1"
        MACOSX_BUNDLE_INFO_STRING "Revere Surveillance System"
    )

    # TODO: Add icon file when available
    # set_target_properties(revere PROPERTIES
    #     MACOSX_BUNDLE_ICON_FILE "revere.icns"
    # )
    # set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/revere.icns")
    # set_source_files_properties(${APP_ICON} PROPERTIES
    #     MACOSX_PACKAGE_LOCATION "Resources")
    # target_sources(revere PRIVATE ${APP_ICON})
endif()

