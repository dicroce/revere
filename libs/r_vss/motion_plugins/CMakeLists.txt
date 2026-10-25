cmake_minimum_required(VERSION 3.14)

# Create motion_plugins output directory in build tree
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/motion_plugins)

# Add plugin subdirectories
if(REVERE_BUILD_TEST_PLUGIN)
    add_subdirectory(test_plugin)
else()
    # A build dir that built it before would otherwise keep bundling its copy.
    file(GLOB _stale_test_plugin "${CMAKE_BINARY_DIR}/motion_plugins/test_plugin.*")
    if(_stale_test_plugin)
        file(REMOVE ${_stale_test_plugin})
    endif()
endif()

#add_subdirectory(ollama_motion_plugin)

# Add NCNN-based plugins only if NCNN is available
if(NCNN_AVAILABLE)
#    add_subdirectory(mobilenet_person_plugin)
#    add_subdirectory(picodet_person_plugin)
    add_subdirectory(yolov8_motion_plugin)
endif()
