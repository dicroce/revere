cmake_minimum_required(VERSION 3.14)
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 1)
set(PROJECT_VERSION_PATCH 4)
project(revere VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

include(TestBigEndian)

# Global Config

set(CMAKE_CXX_STANDARD 17)

if(IS_BIG_ENDIAN)
    add_compile_definitions(IS_BIG_ENDIAN)
else()
    add_compile_definitions(IS_LITTLE_ENDIAN)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Debug)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_compile_definitions(IS_WINDOWS)
    set(VCPKG_TARGET_TRIPLET x64-windows)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_compile_definitions(IS_LINUX)
    set(VCPKG_TARGET_TRIPLET x64-linux-dynamic)
endif()

include(${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake)

function(dump_cmake_variables)
    get_cmake_property(_variableNames VARIABLES)
    list (SORT _variableNames)
    foreach (_variableName ${_variableNames})
        if (ARGV0)
            unset(MATCHED)
            string(REGEX MATCH ${ARGV0} MATCHED ${_variableName})
            if (NOT MATCHED)
                continue()
            endif()
        endif()
        message(STATUS "${_variableName}=${${_variableName}}")
    endforeach()
endfunction()

find_package(PkgConfig)
pkg_check_modules(
    GST REQUIRED
    gstreamer-1.0
    gstreamer-sdp-1.0
    gstreamer-video-1.0
    gstreamer-app-1.0
    gstreamer-plugins-base-1.0
    gstreamer-plugins-bad-1.0
    gstreamer-rtsp-server-1.0
)

find_package(FFMPEG)
find_package(glfw3)
find_package(Threads)
find_package(LibXml2)

#dump_cmake_variables()

# Libraries
#
add_subdirectory(r_utils)
add_subdirectory(r_db)
add_subdirectory(r_http)
add_subdirectory(r_mux)
add_subdirectory(r_codec)
add_subdirectory(r_pipeline)
add_subdirectory(r_fakey)
add_subdirectory(r_storage)
add_subdirectory(r_onvif)
add_subdirectory(r_disco)
add_subdirectory(r_motion)
add_subdirectory(r_vss)
add_subdirectory(imgui)
add_subdirectory(r_ui_utils)

# Applications
#
add_subdirectory(revere)
add_subdirectory(vision)

# Unit Tests
#
#add_subdirectory(ut)

# Tools
#
#add_subdirectory(fakie)

set(CPACK_PACKAGE_NAME "Revere" CACHE STRING "Revere")
set(
    CPACK_PACKAGE_DESCRIPTION_SUMMARY "Desktop video surveillance application"
    CACHE STRING "A free, open source, onvif compatible, cross platform video surveillance system designed to run unobtrusively on standard desktop computers."
)
set(CPACK_PACKAGE_VENDOR "Tony Di Croce")
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
SET(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/_packages")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
set(CPACK_OUTPUT_FILE_PREFIX ".")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_CONTACT "dicroce@gmail.com")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Tony Di Croce")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)#ONE_PER_GROUP)
set(CPACK_DEB_COMPONENT_INSTALL YES)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)

install(
    TARGETS revere
    RUNTIME DESTINATION revere
)

install(
    TARGETS vision
    RUNTIME DESTINATION revere
)

install(
    FILES revere/revere.desktop vision/vision.desktop
    DESTINATION /usr/share/applications
)

install(
    FILES revere/R.png revere/R.ico vision/V.png vision/V.ico DESTINATION revere
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

