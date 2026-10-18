cmake_minimum_required(VERSION 3.14)
project(revere_ut)

# The app's own pieces that don't need a window, compiled in directly.
add_executable(
    revere_ut
    include/test_configure_state.h
    source/test_configure_state.cpp
    ../source/configure_state.cpp
)

target_include_directories(
    revere_ut PUBLIC
    include
    ../include
)

target_link_libraries(
    revere_ut LINK_PUBLIC
    experimentum
    r_utils
    r_ui_utils
)

# Ensure this is built as a console application on Windows
if(WIN32)
    set_target_properties(revere_ut PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE"
    )
endif()
