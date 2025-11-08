cmake_minimum_required(VERSION 3.14)
project(r_av VERSION 0.0.1)

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

# Install the shared library
install(TARGETS r_av
    RUNTIME DESTINATION revere
    LIBRARY DESTINATION revere
    ARCHIVE DESTINATION revere
)

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
endif()

add_subdirectory(ut)
