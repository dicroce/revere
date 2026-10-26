cmake_minimum_required(VERSION 3.14)
project(r_vss)

file(GLOB R_VSS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_vss/*.h
)

file(GLOB R_VSS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_vss SHARED
    ${R_VSS_HEADERS}
    ${R_VSS_SOURCES}
)

# The build version, number and revision from the root CMakeLists (fixed at
# configure time), read through r_vss/r_build_info.h.
target_compile_definitions(r_vss PRIVATE
    REVERE_VERSION_STRING="${REVERE_VERSION_STRING}"
    REVERE_BUILD_NUMBER=${REVERE_BUILD_NUMBER}
    REVERE_GIT_REVISION="${REVERE_GIT_REVISION}"
)

# The build time and dirty flag, stamped on every build: this target always
# runs, rewrites one small .cpp, and only that object recompiles.
set(R_VSS_BUILD_INFO_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/revere_build_info.cpp)
add_custom_target(r_vss_build_info
    COMMAND ${CMAKE_COMMAND}
            -DOUT=${R_VSS_BUILD_INFO_CPP}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/build_info.cmake
    BYPRODUCTS ${R_VSS_BUILD_INFO_CPP}
    COMMENT "Stamping the Revere build time"
    VERBATIM
)
target_sources(r_vss PRIVATE ${R_VSS_BUILD_INFO_CPP})
add_dependencies(r_vss r_vss_build_info)

target_include_directories(
    r_vss PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_vss PUBLIC
    r_pipeline
    r_av
    r_motion
    r_cameras
    r_disco
    r_storage
    r_utils
    ffmpeg::ffmpeg
    opencv::opencv
    gstreamer::gstreamer
)

# Keeping the computer awake (r_keep_awake_*.cpp): IOKit power assertions on
# macOS, power requests on Windows, logind and desktop inhibitors over GDBus on
# Linux.
if(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    target_link_libraries(r_vss PRIVATE "-framework IOKit" "-framework CoreFoundation")
elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
    target_link_libraries(r_vss PRIVATE powrprof)
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_search_module(GIO REQUIRED gio-2.0)
    pkg_search_module(GIO_UNIX REQUIRED gio-unix-2.0)
    target_include_directories(r_vss PRIVATE ${GIO_INCLUDE_DIRS} ${GIO_UNIX_INCLUDE_DIRS})
    target_link_directories(r_vss PRIVATE ${GIO_LIBRARY_DIRS})
    target_link_libraries(r_vss PRIVATE ${GIO_LIBRARIES})
endif()

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_vss
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()

add_subdirectory(motion_plugins)
add_subdirectory(ut)

# Pack the web UI dist/ into a single .rbt bundle using r_bundle.
# dist/ is checked into git so Node.js is NOT required to build Revere.
# To rebuild the UI: cd webui && npm install && npm run build
set(WEBUI_DIST   ${CMAKE_CURRENT_SOURCE_DIR}/webui/dist)
set(WEBUI_BUNDLE ${CMAKE_CURRENT_BINARY_DIR}/ui.rbt)

# r_bundle links against libr_utils. At build time the shared lib lives in its
# own build dir, not next to the binary, so point the loader at it for the call.
# (The baked-in $ORIGIN / @loader_path RPATH only resolves once installed.)
if(APPLE)
    set(_r_bundle_loader_env
        "DYLD_LIBRARY_PATH=$<TARGET_FILE_DIR:r_utils>:$ENV{DYLD_LIBRARY_PATH}")
else()
    set(_r_bundle_loader_env
        "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:r_utils>:$ENV{LD_LIBRARY_PATH}")
endif()
add_custom_command(
    OUTPUT  ${WEBUI_BUNDLE}
    COMMAND ${CMAKE_COMMAND} -E env
            "${_r_bundle_loader_env}"
            $<TARGET_FILE:r_bundle> ${WEBUI_DIST} ${WEBUI_BUNDLE}
    DEPENDS r_bundle ${WEBUI_DIST}/index.html
    COMMENT "Packing Revere web UI bundle"
)

add_custom_target(r_vss_webui ALL DEPENDS ${WEBUI_BUNDLE})

install(FILES ${WEBUI_BUNDLE}
    DESTINATION ${REVERE_INSTALL_BINDIR}
)
