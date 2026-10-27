cmake_minimum_required(VERSION 3.14)
project(rnnoise C)

# Vendored xiph/rnnoise v0.2 (see README.revere). Built static on purpose: all
# state lives in a per-instance DenoiseState and the model weights are const
# data, so there are no globals to duplicate across DLLs.

set(RNNOISE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/denoise.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/rnn.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/pitch.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/kiss_fft.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/celt_lpc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/nnet.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/nnet_default.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/parse_lpcnet_weights.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/rnnoise_data.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/rnnoise_tables.c
)

add_library(
    rnnoise STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rnnoise.h
    ${RNNOISE_SOURCES}
)

set_target_properties(rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(rnnoise PRIVATE RNNOISE_BUILD)

if(MSVC)
    # MSVC never defines __SSE2__, so without these rnnoise falls back to its
    # slow scalar path. SSE2 is baseline on x64.
    if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
        target_compile_definitions(rnnoise PRIVATE OPUS_X86_MAY_HAVE_SSE OPUS_X86_MAY_HAVE_SSE2)
    endif()
    target_compile_definitions(rnnoise PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(rnnoise PRIVATE /W0)
else()
    target_compile_options(rnnoise PRIVATE -w)
endif()

target_include_directories(
    rnnoise
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/source
)

if(NOT WIN32)
    target_link_libraries(rnnoise PRIVATE m)
endif()
