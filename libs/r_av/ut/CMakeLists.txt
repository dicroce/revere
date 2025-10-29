
add_executable(
    r_av_ut
    include/framework.h
    source/framework.cpp
    include/test_r_mux.h
    source/test_r_mux.cpp
    include/test_r_codec.h
    source/test_r_codec.cpp
)

target_include_directories(
    r_av_ut PUBLIC 
    include
    ../include
)

target_link_libraries(
    r_av_ut LINK_PUBLIC 
    r_utils
    r_av
    ffmpeg::ffmpeg
    opencv::opencv
    platform::platform
)
