cmake_minimum_required(VERSION 3.14)
project(r_bundle)

file(GLOB R_BUNDLE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_executable(r_bundle ${R_BUNDLE_SOURCES})

target_compile_features(r_bundle PRIVATE cxx_std_17)

target_link_libraries(
    r_bundle
    r_utils
)

install(
    TARGETS r_bundle
    RUNTIME DESTINATION ${REVERE_INSTALL_BINDIR}
)
