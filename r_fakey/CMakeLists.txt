
add_library(
    r_fakey
    include/r_fakey/r_fake_camera.h
    source/r_fake_camera.cpp
)

target_include_directories(
    r_fakey PUBLIC
    include
    ../r_utils/include
    ../r_mux/include
    ${GST_INCLUDE_DIRS}
)

target_compile_options(
    r_fakey PUBLIC
    ${GST_CFLAGS}
)

target_link_directories(
    r_fakey PUBLIC
    ${GST_LIB_DIRS}
)

target_link_libraries(
    r_fakey
    r_utils
    r_mux
    ${GST_LIBRARIES}
)
