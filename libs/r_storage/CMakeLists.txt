cmake_minimum_required(VERSION 3.14)
project(r_storage VERSION 0.0.1)

include(FetchContent)

FetchContent_Declare(
  nanots
  GIT_REPOSITORY https://github.com/dicroce/nanots.git
  GIT_TAG main
)
FetchContent_MakeAvailable(nanots)

# Install nanots shared library since it's a dependency
install(TARGETS nanots
    RUNTIME DESTINATION revere
    LIBRARY DESTINATION revere
    ARCHIVE DESTINATION revere
)

file(GLOB R_STORAGE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_storage/*.h
)

file(GLOB R_STORAGE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_storage SHARED
    ${R_STORAGE_HEADERS}
    ${R_STORAGE_SOURCES}
)

target_include_directories(
    r_storage PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_storage PUBLIC
    r_db
    r_utils
    nanots
)

# Install the shared library
install(TARGETS r_storage
    RUNTIME DESTINATION revere
    LIBRARY DESTINATION revere
    ARCHIVE DESTINATION revere
)

add_subdirectory(ut)
