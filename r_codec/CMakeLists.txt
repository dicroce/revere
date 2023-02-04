
add_library(
    r_codec
    include/r_codec/r_video_decoder.h
    source/r_video_decoder.cpp
    include/r_codec/r_video_encoder.h
    source/r_video_encoder.cpp
    include/r_codec/r_codec_state.h
)

target_include_directories(
    r_codec PUBLIC
    include
    ../r_utils/include
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(
    r_codec PUBLIC
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(
    r_codec
    r_utils
    ${FFMPEG_LIBRARIES}
)
