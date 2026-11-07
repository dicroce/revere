cmake_minimum_required(VERSION 3.14)
project(r_crypto)

file(GLOB R_CRYPTO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_crypto/*.h
)

file(GLOB R_CRYPTO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_crypto SHARED
    ${R_CRYPTO_HEADERS}
    ${R_CRYPTO_SOURCES}
)

target_include_directories(
    r_crypto PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_crypto
    PUBLIC r_utils
    PRIVATE monocypher
)

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_crypto
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()

add_subdirectory(ut)
