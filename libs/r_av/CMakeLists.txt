cmake_minimum_required(VERSION 3.14)
project(r_av)

file(GLOB R_AV_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_av/*.h
)

file(GLOB R_AV_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_av SHARED
    ${R_AV_HEADERS}
    ${R_AV_SOURCES}
)

target_include_directories(
    r_av PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_av PUBLIC
    r_utils
    ffmpeg::ffmpeg
)

# libsrt, where the build has it (dependencies.cmake): SRT output through it, with link statistics.
if(TARGET srt::srt)
    target_link_libraries(r_av PUBLIC srt::srt)
    target_compile_definitions(r_av PRIVATE R_HAVE_LIBSRT=1)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
        target_link_libraries(r_av PUBLIC ws2_32)
    endif()
endif()

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_av
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()

# Post-build: Copy FFmpeg DLLs to build output directory for testing on Windows
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    file(TO_CMAKE_PATH $ENV{FFMPEG_TOP_DIR} FFMPEG_TOP_DIR)

    add_custom_command(TARGET r_av POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FFMPEG_TOP_DIR}/bin/avcodec-62.dll"
            "${FFMPEG_TOP_DIR}/bin/avformat-62.dll"
            "${FFMPEG_TOP_DIR}/bin/avutil-60.dll"
            "${FFMPEG_TOP_DIR}/bin/swresample-6.dll"
            "${FFMPEG_TOP_DIR}/bin/swscale-9.dll"
            "$<TARGET_FILE_DIR:r_av>"
        COMMENT "Copying FFmpeg DLLs to build output directory"
    )

    # libsrt's DLLs (SRT_TOP_DIR's bin), where the build has libsrt.
    if(TARGET srt::srt AND DEFINED ENV{SRT_TOP_DIR})
        file(TO_CMAKE_PATH $ENV{SRT_TOP_DIR} SRT_TOP_DIR)
        file(GLOB SRT_DLLS "${SRT_TOP_DIR}/bin/*.dll")
        if(SRT_DLLS)
            add_custom_command(TARGET r_av POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SRT_DLLS} "$<TARGET_FILE_DIR:r_av>"
                COMMENT "Copying libsrt DLLs to build output directory"
            )
        endif()
    endif()
endif()

add_subdirectory(ut)
