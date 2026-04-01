cmake_minimum_required(VERSION 3.14)
project(r_storage_ut)

add_executable(
    r_storage_ut
    include/test_r_storage.h
    source/test_r_storage.cpp
)

target_include_directories(
    r_storage_ut PUBLIC
    include
    ../include
)

target_link_libraries(
    r_storage_ut LINK_PUBLIC
    experimentum
    r_storage
    r_db
    r_utils
    platform::platform
)
