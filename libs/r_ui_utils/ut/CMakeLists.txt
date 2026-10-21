cmake_minimum_required(VERSION 3.14)
project(r_ui_utils_ut)

add_executable(
    r_ui_utils_ut
    include/test_window_placement.h
    source/test_window_placement.cpp
    include/test_section_state.h
    source/test_section_state.cpp
    include/test_text_selection.h
    source/test_text_selection.cpp
    include/test_log_tail.h
    source/test_log_tail.cpp
)

target_include_directories(
    r_ui_utils_ut PUBLIC
    include
)

target_link_libraries(
    r_ui_utils_ut LINK_PUBLIC
    experimentum
    r_ui_utils
)

# Ensure this is built as a console application on Windows
if(WIN32)
    set_target_properties(r_ui_utils_ut PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE"
    )
endif()
