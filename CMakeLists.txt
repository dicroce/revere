cmake_minimum_required(VERSION 3.14)
project(revere VERSION 1.0.0)
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
