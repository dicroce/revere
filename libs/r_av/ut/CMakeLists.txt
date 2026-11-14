
add_executable(
    r_av_ut
    include/test_r_mux.h
    source/test_r_mux.cpp
    include/test_r_codec.h
    source/test_r_codec.cpp
    include/test_r_transcoder_repro.h
    source/test_r_transcoder_repro.cpp
    include/test_r_h264_frame_num.h
    source/test_r_h264_frame_num.cpp
    include/test_r_output_frame_selector.h
    source/test_r_output_frame_selector.cpp
    include/test_r_srt_url.h
    source/test_r_srt_url.cpp
    include/test_r_srt_output.h
    source/test_r_srt_output.cpp
)

target_include_directories(
    r_av_ut PUBLIC
    include
    ../include
    ../../ut_data
)

target_link_libraries(
    r_av_ut LINK_PUBLIC
    experimentum
    r_utils
    r_av
    ffmpeg::ffmpeg
    opencv::opencv
    platform::platform
)

if(TARGET srt::srt)
    target_compile_definitions(r_av_ut PRIVATE R_HAVE_LIBSRT=1)
endif()
