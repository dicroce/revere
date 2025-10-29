cmake_minimum_required(VERSION 3.14)
project(r_utils VERSION 0.0.1)

file(GLOB MBEDTLS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/mbedtls/*.c
)

file(GLOB MBEDTLS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/mbedtls/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/psa/*.h
)

file(GLOB R_UTILS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_utils/*.h
)

file(GLOB R_UTILS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_utils SHARED
    ${MBEDTLS_HEADERS}
    ${MBEDTLS_SOURCES}
    ${R_UTILS_HEADERS}
    ${R_UTILS_SOURCES}
)

target_include_directories(
    r_utils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source/mbedtls>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_utils PUBLIC
    uuid::uuid
    platform::platform
)

if(NOT WIN32)
    target_link_libraries(r_utils PUBLIC dl)
endif()

# Install the shared library
install(TARGETS r_utils
    RUNTIME DESTINATION revere
    LIBRARY DESTINATION revere
    ARCHIVE DESTINATION revere
)

add_subdirectory(ut)
