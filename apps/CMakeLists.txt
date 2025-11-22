
add_subdirectory(revere)
add_subdirectory(vision)

set(CPACK_PACKAGE_VENDOR "Tony Di Croce")
set(CPACK_PACKAGE_VERSION_MAJOR 0)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 1)
set(CPACK_PACKAGE_NAME "Revere" CACHE STRING "Revere")
SET(CPACK_SET_DESTDIR TRUE)
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "dicroce@gmail.com")
set(CPACK_PACKAGE_CONTACT "dicroce@gmail.com")
set(CPACK_PACKAGE_VERSION_MAJOR 0)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 1)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
SET(CPACK_OUTPUT_FILE_PREFIX ${CMAKE_BINARY_DIR})
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
set(CPACK_OUTPUT_FILE_PREFIX ".")
#set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
#set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)#ONE_PER_GROUP)
set(CPACK_DEB_COMPONENT_INSTALL YES)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
include(GNUInstallDirs)
set(
    CPACK_PACKAGE_DESCRIPTION_SUMMARY "Desktop video surveillance application"
    CACHE STRING "A free, open source, onvif compatible, cross platform video surveillance system designed to run unobtrusively on standard desktop computers."
)

# ---- Windows-specific CPack Settings ----
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "${CMAKE_PROJECT_NAME} Dev Package")
    set(CPACK_NSIS_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
    set(CPACK_NSIS_CONTACT "dicroce@gmail.com")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CMAKE_PROJECT_NAME}")
    set(CPACK_PACKAGE_EXECUTABLES "your_executable" "YourApp")
    set(CPACK_NSIS_MODIFY_PATH ON) # Adds to user PATH

    include(CPack)
endif()

# ---- Linux-specific CPack Settings ----
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CPACK_GENERATOR "DEB")
    set(CPACK_SET_DESTDIR TRUE)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "dicroce@gmail.com")
    set(CPACK_PACKAGE_CONTACT "dicroce@gmail.com")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
    set(CPACK_VERBATIM_VARIABLES YES)
    set(CPACK_PACKAGE_INSTALL_DIRECTORY ${CPACK_PACKAGE_NAME})
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEB_COMPONENT_INSTALL YES)
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

    include(CPack)
    include(GNUInstallDirs)
endif()

# ---- macOS-specific CPack Settings ----
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "${CMAKE_PROJECT_NAME}")
    set(CPACK_DMG_FORMAT "UDBZ")  # Compressed DMG
    set(CPACK_DMG_BACKGROUND_IMAGE "${CMAKE_CURRENT_SOURCE_DIR}/dmg_background.png")
    # Create a nice DMG with Applications folder link
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/DMGSetup.scpt")

    # App bundles will be installed at the root of the DMG
    set(CPACK_PACKAGE_INSTALL_DIRECTORY ".")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")

    include(CPack)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # Set default install location to /Applications on macOS
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "/Applications" CACHE PATH "Install path prefix" FORCE)
    endif()

    # Install as app bundles on macOS
    install(
        TARGETS revere
        BUNDLE DESTINATION .
    )

    install(
        TARGETS vision
        BUNDLE DESTINATION .
    )

    # Bundle shared libraries, plugins, and models into each app
    install(CODE "
        # Copy all project dylibs from the build directory into both app bundles
        file(GLOB_RECURSE DYLIBS
            \"${CMAKE_BINARY_DIR}/libs/*/lib*.dylib\"
            \"${CMAKE_BINARY_DIR}/_deps/*/lib*.dylib\"
        )
        foreach(DYLIB \${DYLIBS})
            get_filename_component(DYLIB_NAME \${DYLIB} NAME)
            file(INSTALL \${DYLIB} DESTINATION \"\${CMAKE_INSTALL_PREFIX}/Revere.app/Contents/MacOS\")
            file(INSTALL \${DYLIB} DESTINATION \"\${CMAKE_INSTALL_PREFIX}/Vision.app/Contents/MacOS\")
        endforeach()

        # Copy motion plugins into Revere.app only (Vision doesn't use them)
        if(EXISTS \"${CMAKE_BINARY_DIR}/motion_plugins\")
            file(GLOB PLUGINS \"${CMAKE_BINARY_DIR}/motion_plugins/*\")
            file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/Revere.app/Contents/MacOS/motion_plugins\")
            foreach(PLUGIN \${PLUGINS})
                file(INSTALL \${PLUGIN} DESTINATION \"\${CMAKE_INSTALL_PREFIX}/Revere.app/Contents/MacOS/motion_plugins\")
            endforeach()
        endif()

        # Copy model files into Revere.app only
        file(GLOB MODEL_DIRS \"${CMAKE_SOURCE_DIR}/libs/r_vss/motion_plugins/*/model\")
        file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/Revere.app/Contents/MacOS/models\")
        foreach(MODEL_DIR \${MODEL_DIRS})
            get_filename_component(PLUGIN_DIR \${MODEL_DIR} DIRECTORY)
            get_filename_component(PLUGIN_NAME \${PLUGIN_DIR} NAME)
            # Extract plugin name without _plugin suffix for model directory
            string(REPLACE \"_plugin\" \"\" MODEL_NAME \"\${PLUGIN_NAME}\")
            file(INSTALL \${MODEL_DIR}/ DESTINATION \"\${CMAKE_INSTALL_PREFIX}/Revere.app/Contents/MacOS/models/\${MODEL_NAME}\")
        endforeach()

        # Copy system plugins into Revere.app only
        if(EXISTS \"${CMAKE_BINARY_DIR}/system_plugins\")
            file(GLOB SYSTEM_PLUGINS \"${CMAKE_BINARY_DIR}/system_plugins/*\")
            file(MAKE_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/Revere.app/Contents/MacOS/system_plugins\")
            foreach(PLUGIN \${SYSTEM_PLUGINS})
                file(INSTALL \${PLUGIN} DESTINATION \"\${CMAKE_INSTALL_PREFIX}/Revere.app/Contents/MacOS/system_plugins\")
            endforeach()
        endif()
    ")
else()
    # Regular install for Windows and Linux
    install(
        TARGETS revere
        RUNTIME DESTINATION revere
    )

    install(
        TARGETS vision
        RUNTIME DESTINATION revere
    )
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
install(
    FILES vision/vision.desktop revere/revere.desktop
    DESTINATION /usr/share/applications
)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # Install resources into app bundles on macOS
    install(
        FILES revere/R.png
        DESTINATION Revere.app/Contents/Resources
    )
    install(
        FILES vision/V.png
        DESTINATION Vision.app/Contents/Resources
    )
else()
    # Regular install for Windows and Linux
    install(
        FILES vision/V.png vision/V.ico revere/R.png revere/R.ico
        DESTINATION revere
    )
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    file(TO_CMAKE_PATH $ENV{FFMPEG_TOP_DIR} FFMPEG_TOP_DIR)
    file(TO_CMAKE_PATH $ENV{GST_TOP_DIR} GST_TOP_DIR)
    file(TO_CMAKE_PATH $ENV{OPENCV_TOP_DIR} OPENCV_TOP_DIR)
    
    # Define OpenCV modules list for easier maintenance
    set(OPENCV_MODULES
        opencv_bgsegm
        opencv_calib3d
        opencv_core
        opencv_dnn
        opencv_features2d
        opencv_flann
        opencv_imgcodecs
        opencv_imgproc
        opencv_optflow
        opencv_plot
        opencv_tracking
        opencv_video
        opencv_xfeatures2d
        opencv_ximgproc
    )
    
    # Install FFmpeg and GStreamer DLLs (these remain the same regardless of build type)
    install(
        FILES
        ${FFMPEG_TOP_DIR}/bin/avcodec-62.dll
        ${FFMPEG_TOP_DIR}/bin/avformat-62.dll
        ${FFMPEG_TOP_DIR}/bin/avutil-60.dll
        ${FFMPEG_TOP_DIR}/bin/swresample-6.dll
        ${FFMPEG_TOP_DIR}/bin/swscale-9.dll

        ${GST_TOP_DIR}/bin/gio-2.0-0.dll
        ${GST_TOP_DIR}/bin/glib-2.0-0.dll
        ${GST_TOP_DIR}/bin/gmodule-2.0-0.dll
        ${GST_TOP_DIR}/bin/gobject-2.0-0.dll
        ${GST_TOP_DIR}/bin/gstapp-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstaudio-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstbase-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstcodecparsers-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstnet-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstpbutils-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstreamer-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstrtp-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstrtsp-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstrtspserver-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstsdp-1.0-0.dll
        ${GST_TOP_DIR}/bin/gsttag-1.0-0.dll
        ${GST_TOP_DIR}/bin/gstvideo-1.0-0.dll
        ${GST_TOP_DIR}/bin/intl-8.dll
        ${GST_TOP_DIR}/bin/pcre2-8-0.dll
        ${GST_TOP_DIR}/bin/ffi-7.dll
        ${GST_TOP_DIR}/bin/z-1.dll
        ${GST_TOP_DIR}/bin/orc-0.4-0.dll
        DESTINATION revere
    )
    
    # Install OpenCV DLLs conditionally based on build configuration
    # Use generator expressions to handle multi-config generators (like Visual Studio)
    foreach(module ${OPENCV_MODULES})
        install(
            FILES 
                "$<$<CONFIG:Debug>:${OPENCV_TOP_DIR}/x64/vc17/bin/${module}4120d.dll>"
                "$<$<NOT:$<CONFIG:Debug>>:${OPENCV_TOP_DIR}/x64/vc17/bin/${module}4120.dll>"
            DESTINATION revere
            OPTIONAL
        )
    endforeach()
    
    # Install GStreamer plugins (remains the same)
    install(
        FILES
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstapp.dll
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstapp.pdb
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstaudioparsers.dll
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstaudioparsers.pdb
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstcoreelements.dll
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstcoreelements.pdb
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstrtp.dll
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstrtp.pdb
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstrtpmanager.dll
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstrtpmanager.pdb
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstrtsp.dll
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstrtsp.pdb
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gsttcp.dll
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gsttcp.pdb
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstudp.dll
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstudp.pdb
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstvideoparsersbad.dll
        ${GST_TOP_DIR}/lib/gstreamer-1.0/gstvideoparsersbad.pdb
        DESTINATION revere/gstreamer_plugins
    )

    # Install system plugins if they were built
    install(CODE "
        if(EXISTS \"${CMAKE_BINARY_DIR}/system_plugins\")
            file(GLOB SYSTEM_PLUGINS \"${CMAKE_BINARY_DIR}/system_plugins/*\")
            foreach(PLUGIN \${SYSTEM_PLUGINS})
                file(INSTALL \${PLUGIN} DESTINATION \"\${CMAKE_INSTALL_PREFIX}/revere/system_plugins\")
            endforeach()
        endif()
    ")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    # Detect the multiarch library directory (e.g., x86_64-linux-gnu, aarch64-linux-gnu)
    execute_process(
        COMMAND dpkg-architecture -qDEB_HOST_MULTIARCH
        OUTPUT_VARIABLE MULTIARCH_TRIPLET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Fallback to CMAKE_LIBRARY_ARCHITECTURE if dpkg-architecture is not available
    if(NOT MULTIARCH_TRIPLET)
        set(MULTIARCH_TRIPLET ${CMAKE_LIBRARY_ARCHITECTURE})
    endif()

    # Final fallback to x86_64-linux-gnu if neither method works
    if(NOT MULTIARCH_TRIPLET)
        set(MULTIARCH_TRIPLET "x86_64-linux-gnu")
        message(WARNING "Could not detect multiarch triplet, defaulting to ${MULTIARCH_TRIPLET}")
    else()
        message(STATUS "Detected multiarch triplet: ${MULTIARCH_TRIPLET}")
    endif()

    set(GSTREAMER_PLUGIN_DIR "/usr/lib/${MULTIARCH_TRIPLET}/gstreamer-1.0")

    install(
        FILES
        ${GSTREAMER_PLUGIN_DIR}/libgstapp.so
        ${GSTREAMER_PLUGIN_DIR}/libgstaudioparsers.so
        ${GSTREAMER_PLUGIN_DIR}/libgstcoreelements.so
        ${GSTREAMER_PLUGIN_DIR}/libgstrtp.so
        ${GSTREAMER_PLUGIN_DIR}/libgstrtpmanager.so
        ${GSTREAMER_PLUGIN_DIR}/libgstrtsp.so
        ${GSTREAMER_PLUGIN_DIR}/libgsttcp.so
        ${GSTREAMER_PLUGIN_DIR}/libgstudp.so
        ${GSTREAMER_PLUGIN_DIR}/libgstvideoparsersbad.so
        DESTINATION revere/gstreamer_plugins
    )

    # Install system plugins if they were built
    install(CODE "
        if(EXISTS \"${CMAKE_BINARY_DIR}/system_plugins\")
            file(GLOB SYSTEM_PLUGINS \"${CMAKE_BINARY_DIR}/system_plugins/*\")
            foreach(PLUGIN \${SYSTEM_PLUGINS})
                file(INSTALL \${PLUGIN} DESTINATION \"\${CMAKE_INSTALL_PREFIX}/revere/system_plugins\")
            endforeach()
        endif()
    ")
endif()

# macOS: All libraries, plugins, and resources are bundled into the .app bundles
# See the install(CODE ...) section above for macOS app bundle installation
