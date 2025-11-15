cmake_minimum_required(VERSION 3.14)
project(r_db VERSION 0.0.1)

file(GLOB R_DB_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_db/*.h
)

file(GLOB R_DB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.c
)

add_library(
    r_db SHARED
    ${R_DB_HEADERS}
    ${R_DB_SOURCES}
)

target_include_directories(
    r_db PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_db PUBLIC
    r_utils
)

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_db
        RUNTIME DESTINATION revere
        LIBRARY DESTINATION revere
        ARCHIVE DESTINATION revere
    )
endif()

add_subdirectory(ut)
