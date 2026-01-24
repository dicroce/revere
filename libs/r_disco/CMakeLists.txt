cmake_minimum_required(VERSION 3.14)
project(r_disco)

file(GLOB R_DISCO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_disco/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_disco/providers/*.h
)

file(GLOB R_DISCO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/providers/*.cpp
)

add_library(
    r_disco SHARED
    ${R_DISCO_HEADERS}
    ${R_DISCO_SOURCES}
)

target_include_directories(
    r_disco PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_disco PUBLIC
    r_onvif
    r_pipeline
    r_db
    r_utils
    gstreamer::gstreamer
)

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_disco
        RUNTIME DESTINATION revere
        LIBRARY DESTINATION revere
        ARCHIVE DESTINATION revere
    )
endif()
