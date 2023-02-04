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

install(
    FILES vision.desktop 
    DESTINATION /usr/share/applications
)

install(
    FILES V.png V.ico
    DESTINATION revere
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    install(
        FILES
        ${CMAKE_BINARY_DIR}/vision/Debug/avcodec-59.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/avcodec-59.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/avutil-57.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/avutil-57.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gio-2.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gio-2.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/glfw3.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/glfw3.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/glib-2.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/glib-2.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gmodule-2.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gmodule-2.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gobject-2.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gobject-2.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gstapp-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstapp-1.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gstaudio-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstaudio-1.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gstbase-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstbase-1.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gstcodecparsers-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstcodecparsers-1.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gstpbutils-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstpbutils-1.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gstreamer-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstreamer-1.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gstrtp-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstrtp-1.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gstsdp-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstsdp-1.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gsttag-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gsttag-1.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/gstvideo-1.0-0.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/gstvideo-1.0-0.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/iconv-2.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/iconv-2.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/intl-8.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/intl-8.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/libffi.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/libffi.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/pcre2-8d.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/pcre2-8.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/swresample-4.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/swresample-4.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/swscale-6.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/swscale-6.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/zlibd1.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/zlib.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/libx264-164.dll
        ${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/libx264-164.pdb
        ${CMAKE_BINARY_DIR}/vision/Debug/vision.exe
        ${CMAKE_BINARY_DIR}/vision/Debug/vision.pdb
        DESTINATION revere
    )
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    install(
        TARGETS vision
        RUNTIME DESTINATION revere
    )

    install(
        CODE [[
            file(GET_RUNTIME_DEPENDENCIES
                RESOLVED_DEPENDENCIES_VAR RES
                CONFLICTING_DEPENDENCIES_PREFIX CONFLICTING_DEPENDENCIES
                EXECUTABLES $<TARGET_FILE:vision>
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
endif()
