cmake_minimum_required(VERSION 3.14)
project(revere VERSION 0.0.1)

include(settings.cmake)
include(dependencies.cmake)

add_subdirectory(libs)
add_subdirectory(apps)
