cmake_minimum_required(VERSION 3.14)
project(monocypher C)

# Vendored Monocypher 4.0.2 (see README.revere). Built static, and linked
# privately into r_crypto: nothing else includes its headers.

add_library(
    monocypher STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/monocypher.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/monocypher-ed25519.h
    ${CMAKE_CURRENT_SOURCE_DIR}/source/monocypher.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/monocypher-ed25519.c
)

set_target_properties(monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(
    monocypher PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
