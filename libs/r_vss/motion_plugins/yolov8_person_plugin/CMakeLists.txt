cmake_minimum_required(VERSION 3.14)
project(yolov8_person_plugin)

file(GLOB YOLOV8_PERSON_PLUGIN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

file(GLOB YOLOV8_PERSON_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    yolov8_person_plugin SHARED
    ${YOLOV8_PERSON_PLUGIN_HEADERS}
    ${YOLOV8_PERSON_PLUGIN_SOURCES}
)

target_include_directories(
    yolov8_person_plugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    yolov8_person_plugin PRIVATE
    r_utils
    r_disco
    r_vss
    opencv::opencv
    ncnn::ncnn
)

# Add compile-time dependencies for headers only
target_include_directories(
    yolov8_person_plugin PRIVATE
    $<TARGET_PROPERTY:r_vss,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:r_utils,INTERFACE_INCLUDE_DIRECTORIES>
)

# Set the output name to avoid lib prefix on all platforms
set_target_properties(yolov8_person_plugin PROPERTIES PREFIX "")

# Copy the plugin to the motion_plugins directory after build
add_custom_command(TARGET yolov8_person_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:yolov8_person_plugin> ${CMAKE_BINARY_DIR}/motion_plugins/
)

# Install the plugin to motion_plugins subdirectory relative to the executable (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
install(TARGETS yolov8_person_plugin
    LIBRARY DESTINATION revere/motion_plugins
    RUNTIME DESTINATION revere/motion_plugins
)
endif()

# Install model files (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/model/
    DESTINATION revere/models/yolov8_person
    FILES_MATCHING
    PATTERN "*.bin"
    PATTERN "*.param"
)
endif()