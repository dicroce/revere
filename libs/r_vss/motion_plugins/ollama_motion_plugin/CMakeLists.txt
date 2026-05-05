cmake_minimum_required(VERSION 3.14)
project(ollama_motion_plugin)

file(GLOB OLLAMA_MOTION_PLUGIN_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)
file(GLOB OLLAMA_MOTION_PLUGIN_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp)

add_library(ollama_motion_plugin SHARED
    ${OLLAMA_MOTION_PLUGIN_HEADERS}
    ${OLLAMA_MOTION_PLUGIN_SOURCES}
)

target_include_directories(ollama_motion_plugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ollama_motion_plugin PRIVATE
    r_utils
    r_disco
    r_vss
    r_http
    opencv::opencv
)

target_include_directories(ollama_motion_plugin PRIVATE
    $<TARGET_PROPERTY:r_vss,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:r_utils,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:r_http,INTERFACE_INCLUDE_DIRECTORIES>
)

set_target_properties(ollama_motion_plugin PROPERTIES PREFIX "")

add_custom_command(TARGET ollama_motion_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:ollama_motion_plugin>
            ${CMAKE_BINARY_DIR}/motion_plugins/
)

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS ollama_motion_plugin
        LIBRARY DESTINATION revere/motion_plugins
        RUNTIME DESTINATION revere/motion_plugins
    )
endif()
