
add_library(experimentum STATIC
    include/framework.h
    source/framework.cpp
)

target_include_directories(experimentum PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
