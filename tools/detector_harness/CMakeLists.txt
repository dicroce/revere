cmake_minimum_required(VERSION 3.14)
project(detector_harness)

# Developer tool, not installed: runs a detection motion plugin over images
# (see source/main.cpp). Loads the plugin at runtime, so links only OpenCV.
add_executable(detector_harness source/main.cpp)

target_link_libraries(detector_harness PRIVATE opencv::opencv)

if(WIN32)
    target_compile_definitions(detector_harness PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(detector_harness PRIVATE ${CMAKE_DL_LIBS})
endif()

set_target_properties(detector_harness PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
