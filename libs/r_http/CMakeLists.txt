cmake_minimum_required(VERSION 3.14)
project(r_http VERSION 0.0.1)

file(GLOB R_HTTP_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_http/*.h
)

file(GLOB R_HTTP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_http SHARED
    ${R_HTTP_HEADERS}
    ${R_HTTP_SOURCES}
)

target_include_directories(
    r_http PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_http PUBLIC
    r_utils
)

# Install the shared library
install(TARGETS r_http
    RUNTIME DESTINATION revere
    LIBRARY DESTINATION revere
    ARCHIVE DESTINATION revere
)

add_subdirectory(ut)
