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

install(
    TARGETS revere
    RUNTIME DESTINATION revere
)

install(
    FILES revere.desktop 
    DESTINATION /usr/share/applications
)

install(
    FILES R.png R.ico DESTINATION revere
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    install(
        FILES
        ${CMAKE_BINARY_DIR}/revere/Release/avcodec-59.dll
        ${CMAKE_BINARY_DIR}/revere/Release/avformat-59.dll
        ${CMAKE_BINARY_DIR}/revere/Release/avutil-57.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gio-2.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/glfw3.dll
        ${CMAKE_BINARY_DIR}/revere/Release/glib-2.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gmodule-2.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gobject-2.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstapp-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstaudio-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstbase-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstcodecparsers-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstnet-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstpbutils-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstreamer-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstrtp-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstrtp-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstrtsp-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstrtspserver-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstsdp-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gsttag-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/gstvideo-1.0-0.dll
        ${CMAKE_BINARY_DIR}/revere/Release/iconv-2.dll
        ${CMAKE_BINARY_DIR}/revere/Release/intl-8.dll
        ${CMAKE_BINARY_DIR}/revere/Release/libffi.dll
        ${CMAKE_BINARY_DIR}/revere/Release/liblzma.dll
        ${CMAKE_BINARY_DIR}/revere/Release/libxml2.dll
        ${CMAKE_BINARY_DIR}/revere/Release/pcre2-8.dll
        ${CMAKE_BINARY_DIR}/revere/Release/swresample-4.dll
        ${CMAKE_BINARY_DIR}/revere/Release/swscale-6.dll
        ${CMAKE_BINARY_DIR}/revere/Release/zlib1.dll
        ${CMAKE_BINARY_DIR}/revere/Release/libx264-164.dll
        ${CMAKE_BINARY_DIR}/revere/Release/revere.exe
        DESTINATION revere
    )
    install(
        FILES
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstapp.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstapp.pdb
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstaudioparsers.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstaudioparsers.pdb
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstcoreelements.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstcoreelements.pdb
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstrtp.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstrtp.pdb
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstrtpmanager.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstrtpmanager.pdb
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstrtsp.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstrtsp.pdb
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gsttcp.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gsttcp.pdb
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstudp.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstudp.pdb
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstvideoparsersbad.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstvideoparsersbad.pdb
        DESTINATION revere/gstreamer_plugins
    )
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    install(
        CODE [[
            file(GET_RUNTIME_DEPENDENCIES
                RESOLVED_DEPENDENCIES_VAR RES
                CONFLICTING_DEPENDENCIES_PREFIX CONFLICTING_DEPENDENCIES
                EXECUTABLES $<TARGET_FILE:revere>
                POST_EXCLUDE_REGEXES "\\x86_64-linux-gnu"
            )

            foreach(DEP ${RES})
                file(INSTALL
                    DESTINATION ${CMAKE_INSTALL_PREFIX}/revere
                    TYPE SHARED_LIBRARY
                    FOLLOW_SYMLINK_CHAIN
                    FILES ${DEP})
            endforeach()
        ]]
    )

    install(
        FILES
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux-dynamic/lib/gstreamer-1.0/libgstapp.so 
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux-dynamic/lib/gstreamer-1.0/libgstaudioparsers.so 
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux-dynamic/lib/gstreamer-1.0/libgstcoreelements.so 
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux-dynamic/lib/gstreamer-1.0/libgstrtp.so 
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux-dynamic/lib/gstreamer-1.0/libgstrtpmanager.so 
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux-dynamic/lib/gstreamer-1.0/libgstrtsp.so 
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux-dynamic/lib/gstreamer-1.0/libgsttcp.so 
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux-dynamic/lib/gstreamer-1.0/libgstudp.so 
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux-dynamic/lib/gstreamer-1.0/libgstvideoparsersbad.so
        DESTINATION revere/gstreamer_plugins
    )
endif()
