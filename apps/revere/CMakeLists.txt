cmake_minimum_required(VERSION 3.14)

if(APPLE)
    project(revere_app LANGUAGES CXX OBJCXX)
else()
    project(revere_app)
endif()

# Generate version header (uses REVERE_VERSION_STRING, REVERE_GIT_REVISION, REVERE_GIT_DESCRIBE from root)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/revere_version.h.in
    ${CMAKE_BINARY_DIR}/generated/revere_version.h
    @ONLY
)

include(FetchContent)

# Set RPATH - skip for packaged builds (Flatpak/Snap use standard library paths)
if(NOT PACKAGED_BUILD)
    if(APPLE)
        set(CMAKE_INSTALL_RPATH "@executable_path/../lib;@executable_path")
        set(CMAKE_BUILD_RPATH "@executable_path/../lib;@executable_path/../../libs/r_utils;@executable_path/../../libs/r_ui_utils;@executable_path/../../libs/r_av;@executable_path/../../libs/r_pipeline;@executable_path/../../libs/r_storage;@executable_path/../../libs/r_motion;@executable_path/../../libs/r_onvif;@executable_path/../../libs/r_http;@executable_path/../../libs/r_db;@executable_path/../../libs/r_fakey")
    else()
        set(CMAKE_INSTALL_RPATH $ORIGIN)
    endif()
endif()

FetchContent_Declare(
    traypp
    GIT_REPOSITORY https://github.com/dicroce/traypp
    GIT_TAG master
)

FetchContent_MakeAvailable(traypp)

# On macOS, patch traypp to not use template mode so the color icon is preserved
if(APPLE)
    file(READ ${traypp_SOURCE_DIR}/tray/src/core/macos/tray.mm TRAY_MM_CONTENT)
    string(REPLACE "[nsIcon setTemplate:YES]" "[nsIcon setTemplate:NO]" TRAY_MM_CONTENT "${TRAY_MM_CONTENT}")
    file(WRITE ${traypp_SOURCE_DIR}/tray/src/core/macos/tray.mm "${TRAY_MM_CONTENT}")
endif()

FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.15
)

set(PUGIXML_BUILD_SHARED OFF CACHE BOOL "")
set(PUGIXML_INSTALL OFF CACHE BOOL "Disable pugixml install target")

FetchContent_MakeAvailable(pugixml)

file(GLOB REVERE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

file(GLOB REVERE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    add_executable(
        revere WIN32
        ${REVERE_HEADERS}
        ${REVERE_SOURCES}
        revere.rc
    )
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_executable(
        revere MACOSX_BUNDLE
        ${REVERE_HEADERS}
        ${REVERE_SOURCES}
    )
else()
    add_executable(
        revere
        ${REVERE_HEADERS}
        ${REVERE_SOURCES}
    )
endif()

#target_compile_definitions(
#    revere PUBLIC
#    USE_STD_FILESYSTEM
#)

target_include_directories(
    revere PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    revere
    r_ui_utils
    r_disco
    r_onvif
    r_storage
    r_fakey
    r_vss
    r_motion
    r_av
    r_pipeline
    r_db
    r_http
    r_utils
    imgui
    pugixml
    opencv::opencv
    gstreamer::gstreamer
    ffmpeg::ffmpeg
    SDL2::SDL2
    uuid::uuid
    platform::platform
)

# Link tray library on all platforms
target_link_libraries(revere tray)

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(revere PUBLIC -rdynamic -ggdb3 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fasynchronous-unwind-tables -Wl,--whole-archive)
    endif()
endif()

# Linux XDG install (desktop file, icon, metainfo)
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    include(GNUInstallDirs)

    # Desktop file
    install(FILES ${CMAKE_SOURCE_DIR}/flatpak/io.github.dicroce.Revere.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

    # Icon
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/R.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps
        RENAME io.github.dicroce.Revere.png)

    # AppStream metainfo
    install(FILES ${CMAKE_SOURCE_DIR}/flatpak/io.github.dicroce.Revere.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
endif()

# macOS app bundle configuration
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set_target_properties(revere PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.revere.app"
        MACOSX_BUNDLE_BUNDLE_NAME "Revere"
        MACOSX_BUNDLE_BUNDLE_VERSION "${REVERE_VERSION_STRING}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${REVERE_VERSION_STRING}"
        MACOSX_BUNDLE_INFO_STRING "Revere Surveillance System"
    )

    # TODO: Add icon file when available
    # set_target_properties(revere PROPERTIES
    #     MACOSX_BUNDLE_ICON_FILE "revere.icns"
    # )
    # set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/revere.icns")
    # set_source_files_properties(${APP_ICON} PROPERTIES
    #     MACOSX_PACKAGE_LOCATION "Resources")
    # target_sources(revere PRIVATE ${APP_ICON})

    # Copy shared libraries into the app bundle so it can find them at runtime
    set(REVERE_LIBS r_utils r_ui_utils r_av r_pipeline r_storage r_motion r_onvif r_http r_db r_fakey r_disco r_vss)
    foreach(LIB ${REVERE_LIBS})
        add_custom_command(TARGET revere POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_BUNDLE_DIR:revere>/Contents/lib
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${LIB}> $<TARGET_BUNDLE_DIR:revere>/Contents/lib/
        )
    endforeach()

    # Also copy SDL2 and nanots
    add_custom_command(TARGET revere POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2> $<TARGET_BUNDLE_DIR:revere>/Contents/lib/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:nanots> $<TARGET_BUNDLE_DIR:revere>/Contents/lib/
    )

    # Copy tray icon into the app bundle
    add_custom_command(TARGET revere POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/R.png $<TARGET_BUNDLE_DIR:revere>/Contents/MacOS/R.png
    )
endif()

