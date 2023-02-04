
add_executable(
    fakie
    include/utils.h
    source/utils.cpp
    source/main.cpp
)
target_include_directories(
    fakie PUBLIC
    include
    ../r_fakey/include
)
target_link_libraries(
    fakie
    r_fakey
    r_utils
)
