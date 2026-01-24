cmake_minimum_required(VERSION 3.14)
project(r_utils_ut)

add_executable(
    ut
    include/framework.h
    source/framework.cpp
    include/test_r_utils.h
    source/test_r_utils.cpp
)

target_include_directories(
    ut PUBLIC 
    include
    ../r_utils/include
)

target_link_libraries(
    ut LINK_PUBLIC
    r_utils
    uuid::uuid
)

# Ensure this is built as a console application on Windows
if(WIN32)
    set_target_properties(ut PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE"
    )
endif()
