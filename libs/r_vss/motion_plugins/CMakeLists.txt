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
    add_subdirectory(yolox_motion_plugin)
endif()

# The host loads every plugin in motion_plugins/: drop a detector a previous
# build left there that this one doesn't build (yolov8, removed for its
# AGPL-3.0 weights, or yolox when NCNN isn't found), so no stale detector
# reports objects.
foreach(_detector yolox yolov8)
    if(NOT (NCNN_AVAILABLE AND _detector STREQUAL "yolox"))
        # REVERE_RUNTIME_DIR is bin/ or, multi-config, bin/$<CONFIG>/ (a
        # generator expression a configure-time glob can't expand).
        file(GLOB _stale_detector
            "${CMAKE_BINARY_DIR}/motion_plugins/${_detector}_motion_plugin.*"
            "${CMAKE_BINARY_DIR}/bin/motion_plugins/${_detector}_motion_plugin.*"
            "${CMAKE_BINARY_DIR}/bin/*/motion_plugins/${_detector}_motion_plugin.*")
        if(_stale_detector)
            file(REMOVE ${_stale_detector})
        endif()
    endif()
endforeach()
