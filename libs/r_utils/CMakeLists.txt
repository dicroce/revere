cmake_minimum_required(VERSION 3.14)
project(r_utils)

include(FetchContent)

# Fetch mbedtls 3.6.3 from official repository
FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.6.3
)

# Configure mbedtls to build static libraries only
set(ENABLE_PROGRAMS OFF CACHE BOOL "Disable mbedtls programs")
set(ENABLE_TESTING OFF CACHE BOOL "Disable mbedtls tests")
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "Disable fatal warnings")
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "Build static mbedtls libraries")
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "Disable shared mbedtls libraries")

# Point mbedtls to our custom configuration files
set(MBEDTLS_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/include/mbedtls/mbedtls_config.h" CACHE STRING "Custom mbedtls config")
set(MBEDTLS_USER_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/include/mbedtls/user_config.h" CACHE STRING "Custom user config")

# GCC 15+ no longer zero-fills the non-first members of a union initialized
# with {0}, leaving mbedtls 3.6.x PSA operation structs with garbage in
# ctx.hmac.hash.id — every TLS 1.3 handshake then fails at the Finished
# message with MBEDTLS_ERR_SSL_INTERNAL_ERROR (PSA_ERROR_BAD_STATE).
include(CheckCCompilerFlag)
check_c_compiler_flag("-fzero-init-padding-bits=unions" C_HAS_ZERO_INIT_PADDING_BITS)
if(C_HAS_ZERO_INIT_PADDING_BITS)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fzero-init-padding-bits=unions>)
endif()

# Windows builds mbedtls with MBEDTLS_THREADING_ALT (see include/mbedtls/
# user_config.h); mbedtls/threading.h then does #include "threading_alt.h",
# which lives next to our config. Directory-scoped so the mbedtls targets
# created by FetchContent_MakeAvailable below pick it up.
if(WIN32)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/mbedtls)
endif()

FetchContent_MakeAvailable(mbedtls)

# Every consumer of the mbedtls headers (not just r_utils: libdatachannel in
# revere_cloud includes them too) must find threading_alt.h, so attach the
# directory to the mbedtls targets themselves and let it propagate.
if(WIN32)
    foreach(_mbed_target mbedcrypto mbedx509 mbedtls)
        target_include_directories(${_mbed_target} PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/mbedtls>)
    endforeach()
endif()

file(GLOB R_UTILS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_utils/*.h
)

file(GLOB R_UTILS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_utils SHARED
    ${R_UTILS_HEADERS}
    ${R_UTILS_SOURCES}
)

target_include_directories(
    r_utils PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${mbedtls_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Consumers of r_utils headers reach mbedtls/threading.h transitively and need
# threading_alt.h too (Windows only, see above).
if(WIN32)
    target_include_directories(
        r_utils PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/mbedtls>
        $<INSTALL_INTERFACE:include/mbedtls>
    )
endif()

target_link_libraries(
    r_utils PUBLIC
    uuid::uuid
    platform::platform
    mbedcrypto
    mbedx509
    mbedtls
)

if(WIN32)
    # Windows-specific libraries for r_secure_store (DPAPI and SHGetFolderPath)
    target_link_libraries(r_utils PUBLIC crypt32 shell32)
elseif(APPLE)
    # macOS-specific frameworks for r_startup (Login Items)
    find_package(Threads REQUIRED)
    target_link_libraries(r_utils PUBLIC dl Threads::Threads "-framework CoreFoundation" "-framework CoreServices")
else()
    find_package(Threads REQUIRED)
    target_link_libraries(r_utils PUBLIC dl Threads::Threads)
endif()

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_utils
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()

add_subdirectory(ut)
