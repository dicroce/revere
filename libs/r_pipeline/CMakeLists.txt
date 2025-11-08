cmake_minimum_required(VERSION 3.14)
project(r_pipeline VERSION 0.0.1)

file(GLOB R_PIPELINE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_pipeline/*.h
)

file(GLOB R_PIPELINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_pipeline SHARED
    ${R_PIPELINE_HEADERS}
    ${R_PIPELINE_SOURCES}
)

target_include_directories(
    r_pipeline PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_pipeline PUBLIC
    r_utils
    gstreamer::gstreamer
)

# Install the shared library
install(TARGETS r_pipeline
    RUNTIME DESTINATION revere
    LIBRARY DESTINATION revere
    ARCHIVE DESTINATION revere
)

# Post-build: Copy GStreamer DLLs to build output directory for testing on Windows
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    file(TO_CMAKE_PATH $ENV{GST_TOP_DIR} GST_TOP_DIR)

    add_custom_command(TARGET r_pipeline POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GST_TOP_DIR}/bin/gio-2.0-0.dll"
            "${GST_TOP_DIR}/bin/glib-2.0-0.dll"
            "${GST_TOP_DIR}/bin/gmodule-2.0-0.dll"
            "${GST_TOP_DIR}/bin/gobject-2.0-0.dll"
            "${GST_TOP_DIR}/bin/gstapp-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstaudio-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstbase-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstcodecparsers-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstnet-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstpbutils-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstreamer-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstrtp-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstrtsp-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstrtspserver-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstsdp-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gsttag-1.0-0.dll"
            "${GST_TOP_DIR}/bin/gstvideo-1.0-0.dll"
            "${GST_TOP_DIR}/bin/intl-8.dll"
            "${GST_TOP_DIR}/bin/pcre2-8-0.dll"
            "${GST_TOP_DIR}/bin/ffi-7.dll"
            "${GST_TOP_DIR}/bin/z-1.dll"
            "${GST_TOP_DIR}/bin/orc-0.4-0.dll"
            "$<TARGET_FILE_DIR:r_pipeline>"
        COMMENT "Copying GStreamer DLLs to build output directory"
    )

    # Copy GStreamer plugins to a subdirectory
    add_custom_command(TARGET r_pipeline POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:r_pipeline>/gstreamer_plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GST_TOP_DIR}/lib/gstreamer-1.0/gstapp.dll"
            "${GST_TOP_DIR}/lib/gstreamer-1.0/gstaudioparsers.dll"
            "${GST_TOP_DIR}/lib/gstreamer-1.0/gstcoreelements.dll"
            "${GST_TOP_DIR}/lib/gstreamer-1.0/gstrtp.dll"
            "${GST_TOP_DIR}/lib/gstreamer-1.0/gstrtpmanager.dll"
            "${GST_TOP_DIR}/lib/gstreamer-1.0/gstrtsp.dll"
            "${GST_TOP_DIR}/lib/gstreamer-1.0/gsttcp.dll"
            "${GST_TOP_DIR}/lib/gstreamer-1.0/gstudp.dll"
            "${GST_TOP_DIR}/lib/gstreamer-1.0/gstvideoparsersbad.dll"
            "$<TARGET_FILE_DIR:r_pipeline>/gstreamer_plugins"
        COMMENT "Copying GStreamer plugins to build output directory"
    )
endif()

add_subdirectory(ut)
