cmake_minimum_required(VERSION 3.14)
project(vision)

# Set RPATH - skip for packaged builds (Flatpak/Snap use standard library paths)
if(NOT PACKAGED_BUILD)
    if(APPLE)
        set(CMAKE_INSTALL_RPATH "@executable_path/../lib;@executable_path")
        set(CMAKE_BUILD_RPATH "@executable_path/../lib;@executable_path/../../libs/r_utils;@executable_path/../../libs/r_ui_utils;@executable_path/../../libs/r_av;@executable_path/../../libs/r_pipeline;@executable_path/../../libs/r_storage;@executable_path/../../libs/r_motion;@executable_path/../../libs/r_onvif;@executable_path/../../libs/r_http;@executable_path/../../libs/r_db")
    else()
        set(CMAKE_INSTALL_RPATH $ORIGIN)
    endif()
endif()

file(GLOB VISION_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

file(GLOB VISION_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_executable(
        vision WIN32
        ${VISION_HEADERS}
        ${VISION_SOURCES}
        vision.rc
    )
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_executable(
        vision MACOSX_BUNDLE
        ${VISION_HEADERS}
        ${VISION_SOURCES}
    )
else()
    add_executable(
        vision
        ${VISION_HEADERS}
        ${VISION_SOURCES}
    )
endif()

#target_compile_definitions(
#    vision PUBLIC
#    USE_STD_FILESYSTEM
#)

target_include_directories(
    vision PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    vision
    r_ui_utils
    r_av
    r_pipeline
    r_db
    r_http
    r_utils
    r_vss
    imgui
    gstreamer::gstreamer
    ffmpeg::ffmpeg
    SDL2::SDL2
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(vision PUBLIC -rdynamic -ggdb3 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fasynchronous-unwind-tables -Wl,--whole-archive)
    endif()
endif()

# Linux XDG install (desktop file, icon)
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    include(GNUInstallDirs)

    # Desktop file
    install(FILES ${CMAKE_SOURCE_DIR}/flatpak/io.github.dicroce.Revere.Vision.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

    # Icon
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/V.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps
        RENAME io.github.dicroce.Revere.Vision.png)
endif()

# macOS app bundle configuration
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set_target_properties(vision PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.revere.vision"
        MACOSX_BUNDLE_BUNDLE_NAME "Vision"
        MACOSX_BUNDLE_BUNDLE_VERSION "${REVERE_VERSION_STRING}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${REVERE_VERSION_STRING}"
        MACOSX_BUNDLE_INFO_STRING "Revere Vision"
    )

    # TODO: Add icon file when available
    # set_target_properties(vision PROPERTIES
    #     MACOSX_BUNDLE_ICON_FILE "vision.icns"
    # )
    # set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/vision.icns")
    # set_source_files_properties(${APP_ICON} PROPERTIES
    #     MACOSX_PACKAGE_LOCATION "Resources")
    # target_sources(vision PRIVATE ${APP_ICON})

    # Copy shared libraries into the app bundle so it can find them at runtime
    set(VISION_LIBS r_utils r_ui_utils r_av r_pipeline r_storage r_motion r_onvif r_http r_db r_vss r_disco r_fakey)
    foreach(LIB ${VISION_LIBS})
        add_custom_command(TARGET vision POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_DIR:vision>/Contents/lib
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${LIB}> $<TARGET_BUNDLE_DIR:vision>/Contents/lib/
        )
    endforeach()

    # Also copy SDL2 and nanots
    add_custom_command(TARGET vision POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2> $<TARGET_BUNDLE_DIR:vision>/Contents/lib/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:nanots> $<TARGET_BUNDLE_DIR:vision>/Contents/lib/
    )
endif()

