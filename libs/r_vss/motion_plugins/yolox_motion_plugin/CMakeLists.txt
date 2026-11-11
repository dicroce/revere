cmake_minimum_required(VERSION 3.14)
project(yolox_motion_plugin)

file(GLOB YOLOX_MOTION_PLUGIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

file(GLOB YOLOX_MOTION_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    yolox_motion_plugin SHARED
    ${YOLOX_MOTION_PLUGIN_HEADERS}
    ${YOLOX_MOTION_PLUGIN_SOURCES}
)

target_include_directories(
    yolox_motion_plugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    yolox_motion_plugin PRIVATE
    r_utils
    r_disco
    r_vss
    opencv::opencv
    ncnn::ncnn
)

# Add compile-time dependencies for headers only
target_include_directories(
    yolox_motion_plugin PRIVATE
    $<TARGET_PROPERTY:r_vss,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:r_utils,INTERFACE_INCLUDE_DIRECTORIES>
)

# Set the output name to avoid lib prefix on all platforms
set_target_properties(yolox_motion_plugin PROPERTIES PREFIX "")

# Copy the plugin to the motion_plugins directory after build (the macOS app
# bundle takes its plugins from there: apps/revere)
add_custom_command(TARGET yolox_motion_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:yolox_motion_plugin> ${CMAKE_BINARY_DIR}/motion_plugins/
)

# And beside the executable, where revere run from the build tree loads it.
if(NOT APPLE)
    add_custom_command(TARGET yolox_motion_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${REVERE_RUNTIME_DIR}/motion_plugins
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:yolox_motion_plugin> ${REVERE_RUNTIME_DIR}/motion_plugins/
        # Its model too (models/ beside the executable, as installed).
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/model ${REVERE_RUNTIME_DIR}/models/yolox
    )
endif()

# Install the plugin to motion_plugins subdirectory relative to the executable (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
install(TARGETS yolox_motion_plugin
    LIBRARY DESTINATION revere/motion_plugins
    RUNTIME DESTINATION revere/motion_plugins
)
endif()

# Install model files (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/model/
    DESTINATION revere/models/yolox
    FILES_MATCHING
    PATTERN "*.bin"
    PATTERN "*.param"
)
endif()