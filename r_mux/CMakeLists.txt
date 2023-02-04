
add_library(
    r_mux
    include/r_mux/r_demuxer.h
    source/r_demuxer.cpp
    include/r_mux/r_muxer.h
    source/r_muxer.cpp
    include/r_mux/r_format_utils.h
    source/r_format_utils.cpp
)

target_include_directories(
    r_mux PUBLIC
    include
    ../r_utils/include
    ${FFMPEG_INCLUDE_DIRS}
)

target_link_directories(
    r_mux PUBLIC
    ${FFMPEG_LIBRARY_DIRS}
)

target_link_libraries(
    r_mux
    r_utils
    ${FFMPEG_LIBRARIES}
)
