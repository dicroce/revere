cmake_minimum_required(VERSION 3.14)
project(r_onvif VERSION 0.0.1)

include(FetchContent)

FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.15
)

set(PUGIXML_BUILD_SHARED OFF CACHE BOOL "")
set(PUGIXML_INSTALL OFF CACHE BOOL "Disable pugixml install target")

FetchContent_MakeAvailable(pugixml)

file(GLOB R_ONVIF_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_onvif/*.h
)

file(GLOB R_ONVIF_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_onvif SHARED
    ${R_ONVIF_HEADERS}
    ${R_ONVIF_SOURCES}
)

target_include_directories(
    r_onvif PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${pugixml_SOURCE_DIR}/src
)

target_link_libraries(
    r_onvif PUBLIC
    r_http
    r_utils
    pugixml::static
)

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_onvif
        RUNTIME DESTINATION revere
        LIBRARY DESTINATION revere
        ARCHIVE DESTINATION revere
    )
endif()

add_subdirectory(ut)
