cmake_minimum_required(VERSION 3.14)
project(r_http)

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

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_http
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()

add_subdirectory(ut)
