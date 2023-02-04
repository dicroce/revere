
add_library(
    r_pipeline
    include/r_pipeline/r_arg.h
    source/r_arg.cpp
    include/r_pipeline/r_gst_source.h
    source/r_gst_source.cpp
    include/r_pipeline/r_stream_info.h
    source/r_stream_info.cpp
    include/r_pipeline/r_sample_context.h
    source/r_sample_context.cpp
    include/r_pipeline/r_gst_buffer.h
    source/r_gst_buffer.cpp
)

target_include_directories(
    r_pipeline PUBLIC
    include
    r_utils/include
    ${GST_INCLUDE_DIRS}
)

target_compile_options(
    r_pipeline PUBLIC
    ${GST_CFLAGS}
)

target_link_directories(
    r_pipeline PUBLIC
    ${GST_LIBRARY_DIRS}
)

target_link_libraries(
    r_pipeline
    r_utils
    ${GST_LIBRARIES}
)
