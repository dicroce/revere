
add_library(
    r_motion
    include/r_motion/utils.h
    source/utils.cpp
    include/r_motion/r_motion_state.h
    source/r_motion_state.cpp
)

target_include_directories(
    r_motion PUBLIC
    include
    ../r_utils/include
)

target_link_libraries(
    r_motion
    r_utils
)
