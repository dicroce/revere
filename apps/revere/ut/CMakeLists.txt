cmake_minimum_required(VERSION 3.14)
project(revere_ut)

# The app's own pieces that don't need a window, compiled in directly.
add_executable(
    revere_ut
    include/test_configure_state.h
    source/test_configure_state.cpp
    include/test_browser_url.h
    source/test_browser_url.cpp
    ../source/configure_state.cpp
    include/test_tray_status.h
    source/test_tray_status.cpp
    include/test_plugin_status_line.h
    source/test_plugin_status_line.cpp
    ../source/tray_status.cpp
    ../source/browser_url.cpp
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
    # main_window.h's headers (test_plugin_status_line)
    r_storage
    r_disco
    r_vss
)

# Ensure this is built as a console application on Windows
if(WIN32)
    set_target_properties(revere_ut PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE"
    )
endif()
