cmake_minimum_required(VERSION 3.14)

# Create motion_plugins output directory in build tree
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/motion_plugins)

# Add plugin subdirectories
add_subdirectory(test_plugin)

# Add NCNN-based plugins only if NCNN is available
if(NCNN_AVAILABLE)
#    add_subdirectory(mobilenet_person_plugin)
#    add_subdirectory(picodet_person_plugin)
    add_subdirectory(yolov8_person_plugin)
endif()
