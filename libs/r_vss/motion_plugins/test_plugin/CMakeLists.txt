cmake_minimum_required(VERSION 3.14)
project(test_plugin)

file(GLOB TEST_PLUGIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

file(GLOB TEST_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    test_plugin SHARED
    ${TEST_PLUGIN_HEADERS}
    ${TEST_PLUGIN_SOURCES}
)

target_include_directories(
    test_plugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    test_plugin PRIVATE
    r_utils
)

# Add compile-time dependencies for headers only
target_include_directories(
    test_plugin PRIVATE
    $<TARGET_PROPERTY:r_vss,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:r_utils,INTERFACE_INCLUDE_DIRECTORIES>
)

# Set the output name to avoid lib prefix on all platforms
set_target_properties(test_plugin PROPERTIES PREFIX "")

# Copy the plugin to the motion_plugins directory after build
add_custom_command(TARGET test_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:test_plugin> ${CMAKE_BINARY_DIR}/motion_plugins/
)

# Install the plugin to motion_plugins subdirectory relative to the executable (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS test_plugin
        LIBRARY DESTINATION revere/motion_plugins
        RUNTIME DESTINATION revere/motion_plugins
    )
endif()