
include(FindPkgConfig REQUIRED)

# ---- Platform target (to abstract away OS-specific system libs) ----
if(NOT TARGET platform::platform)
    add_library(platform::platform INTERFACE IMPORTED GLOBAL)

    if(WIN32)
        target_link_libraries(platform::platform INTERFACE
            ws2_32
            iphlpapi
            crypt32
            bcrypt
            rpcrt4
            DbgHelp
        )
    elseif(UNIX)
        find_package(Threads REQUIRED)
        target_link_libraries(platform::platform INTERFACE
            Threads::Threads
            ${CMAKE_DL_LIBS}
        )
    endif()
endif()

# ---- OpenCV ----
if(NOT TARGET opencv::opencv)
    # Check for explicit path first (works on all platforms)
    if(DEFINED ENV{OPENCV_TOP_DIR})
        set(OPENCV_ROOT "$ENV{OPENCV_TOP_DIR}")
        message(STATUS "Using OpenCV from OPENCV_TOP_DIR: ${OPENCV_ROOT}")

        add_library(opencv::opencv INTERFACE IMPORTED GLOBAL)
        target_compile_definitions(opencv::opencv INTERFACE OPENCV_STATIC)

        # Detect OpenCV4 include path layout (include/opencv4 vs include)
        if(EXISTS "${OPENCV_ROOT}/include/opencv4")
            set(OPENCV_INCLUDE_DIR "${OPENCV_ROOT}/include/opencv4")
        else()
            set(OPENCV_INCLUDE_DIR "${OPENCV_ROOT}/include")
        endif()
        target_include_directories(opencv::opencv INTERFACE "${OPENCV_INCLUDE_DIR}")

        if(CMAKE_SYSTEM_NAME MATCHES "Windows")
            # Windows: Check for vc17 first, fall back to vc16
            if(EXISTS "${OPENCV_ROOT}/x64/vc17/lib")
                set(OPENCV_LIB_DIR "${OPENCV_ROOT}/x64/vc17/lib")
            elseif(EXISTS "${OPENCV_ROOT}/x64/vc16/lib")
                set(OPENCV_LIB_DIR "${OPENCV_ROOT}/x64/vc16/lib")
            else()
                message(FATAL_ERROR "Could not find OpenCV lib directory at ${OPENCV_ROOT}/x64/vc17/lib or ${OPENCV_ROOT}/x64/vc16/lib")
            endif()

            target_link_directories(opencv::opencv INTERFACE
                $<$<CONFIG:Debug>:${OPENCV_LIB_DIR}>
                $<$<CONFIG:Release>:${OPENCV_LIB_DIR}>
                $<$<CONFIG:RelWithDebInfo>:${OPENCV_LIB_DIR}>
                $<$<CONFIG:MinSizeRel>:${OPENCV_LIB_DIR}>
            )

            # Check if opencv_world exists (monolithic build) or individual modules
            if(EXISTS "${OPENCV_LIB_DIR}/opencv_world4120.lib")
                message(STATUS "Using OpenCV world (monolithic) library")
                target_link_libraries(opencv::opencv INTERFACE
                    $<$<CONFIG:Debug>:opencv_world4120d.lib>
                    $<$<CONFIG:Release>:opencv_world4120.lib>
                    $<$<CONFIG:RelWithDebInfo>:opencv_world4120.lib>
                    $<$<CONFIG:MinSizeRel>:opencv_world4120.lib>
                )
            else()
                message(STATUS "Using OpenCV individual module libraries")
                set(OPENCV_MODULES
                    opencv_bgsegm opencv_calib3d opencv_core opencv_dnn
                    opencv_features2d opencv_flann opencv_imgcodecs opencv_imgproc
                    opencv_optflow opencv_plot opencv_tracking opencv_video
                    opencv_xfeatures2d opencv_ximgproc
                )
                foreach(module ${OPENCV_MODULES})
                    target_link_libraries(opencv::opencv INTERFACE
                        $<$<CONFIG:Debug>:${module}4120d.lib>
                        $<$<CONFIG:Release>:${module}4120.lib>
                        $<$<CONFIG:RelWithDebInfo>:${module}4120.lib>
                        $<$<CONFIG:MinSizeRel>:${module}4120.lib>
                    )
                endforeach()
            endif()

            target_link_libraries(opencv::opencv INTERFACE vfw32.lib comctl32.lib)
        else()
            # Linux/macOS with explicit path
            # Check for lib64 first (Flatpak), then lib
            if(EXISTS "${OPENCV_ROOT}/lib64")
                set(OPENCV_LIB_DIR "${OPENCV_ROOT}/lib64")
            else()
                set(OPENCV_LIB_DIR "${OPENCV_ROOT}/lib")
            endif()
            target_link_directories(opencv::opencv INTERFACE "${OPENCV_LIB_DIR}")
            # Try to find what libraries exist
            file(GLOB OPENCV_LIBS "${OPENCV_LIB_DIR}/libopencv_*.so" "${OPENCV_LIB_DIR}/libopencv_*.dylib" "${OPENCV_LIB_DIR}/libopencv_*.a")
            if(OPENCV_LIBS)
                foreach(lib ${OPENCV_LIBS})
                    get_filename_component(lib_name ${lib} NAME_WE)
                    string(REGEX REPLACE "^lib" "" lib_name ${lib_name})
                    target_link_libraries(opencv::opencv INTERFACE ${lib_name})
                endforeach()
            else()
                # Fallback to common module names
                target_link_libraries(opencv::opencv INTERFACE
                    opencv_core opencv_imgproc opencv_imgcodecs opencv_video
                    opencv_bgsegm opencv_tracking opencv_optflow opencv_ximgproc
                )
            endif()
        endif()

    elseif(CMAKE_SYSTEM_NAME MATCHES "Linux|Darwin")
        # Fallback to system packages via pkg-config
        pkg_search_module(OPENCV opencv4)

        if(OPENCV_FOUND)
            add_library(opencv::opencv INTERFACE IMPORTED GLOBAL)
            target_include_directories(opencv::opencv INTERFACE ${OPENCV_INCLUDE_DIRS})
            target_link_directories(opencv::opencv INTERFACE ${OPENCV_LIBRARY_DIRS})
            target_link_libraries(opencv::opencv INTERFACE ${OPENCV_LIBRARIES})
            message(STATUS "Using system OpenCV via pkg-config")
        else()
            message(WARNING "OpenCV not found - motion detection features will be limited")
            add_library(opencv::opencv INTERFACE IMPORTED GLOBAL)
        endif()

    else()
        message(FATAL_ERROR "OPENCV_TOP_DIR must be set on Windows.")
    endif()
endif()

# ---- GStreamer ----
if(NOT TARGET gstreamer::gstreamer)
    # Check for explicit path first (works on all platforms)
    if(DEFINED ENV{GST_TOP_DIR})
        set(GST_ROOT "$ENV{GST_TOP_DIR}")
        message(STATUS "Using GStreamer from GST_TOP_DIR: ${GST_ROOT}")

        add_library(gstreamer::gstreamer INTERFACE IMPORTED GLOBAL)

        if(CMAKE_SYSTEM_NAME MATCHES "Windows")
            target_include_directories(gstreamer::gstreamer INTERFACE
                "${GST_ROOT}/include/gstreamer-1.0"
                "${GST_ROOT}/include/glib-2.0"
                "${GST_ROOT}/lib/glib-2.0/include"
            )
            target_link_directories(gstreamer::gstreamer INTERFACE "${GST_ROOT}/lib")
            target_link_libraries(gstreamer::gstreamer INTERFACE
                gstreamer-1.0.lib gstbase-1.0.lib gstapp-1.0.lib gstvideo-1.0.lib
                gstpbutils-1.0.lib gstrtsp-1.0.lib gstrtspserver-1.0.lib
                gstcodecparsers-1.0.lib gstsdp-1.0.lib glib-2.0.lib gobject-2.0.lib
            )
        else()
            # Linux/macOS with explicit path - use system glib
            pkg_search_module(GLIB REQUIRED glib-2.0)
            pkg_search_module(GOBJECT REQUIRED gobject-2.0)

            # Detect multiarch lib path (Debian/Ubuntu use lib/x86_64-linux-gnu/)
            if(EXISTS "${GST_ROOT}/lib/x86_64-linux-gnu")
                set(GST_LIB_DIR "${GST_ROOT}/lib/x86_64-linux-gnu")
            else()
                set(GST_LIB_DIR "${GST_ROOT}/lib")
            endif()

            target_include_directories(gstreamer::gstreamer INTERFACE
                "${GST_ROOT}/include/gstreamer-1.0"
                ${GLIB_INCLUDE_DIRS}
                ${GOBJECT_INCLUDE_DIRS}
            )
            target_link_directories(gstreamer::gstreamer INTERFACE
                "${GST_LIB_DIR}"
                ${GLIB_LIBRARY_DIRS}
                ${GOBJECT_LIBRARY_DIRS}
            )
            target_link_libraries(gstreamer::gstreamer INTERFACE
                gstreamer-1.0 gstbase-1.0 gstapp-1.0 gstvideo-1.0
                gstpbutils-1.0 gstrtsp-1.0 gstrtspserver-1.0
                gstcodecparsers-1.0 gstsdp-1.0
                ${GLIB_LIBRARIES} ${GOBJECT_LIBRARIES}
            )
        endif()

    elseif(CMAKE_SYSTEM_NAME MATCHES "Linux|Darwin")
        # Fallback to system packages via pkg-config
        pkg_search_module(GST         REQUIRED gstreamer-1.0)
        pkg_search_module(GST_BASE    REQUIRED gstreamer-base-1.0)
        pkg_search_module(GST_APP     REQUIRED gstreamer-app-1.0)
        pkg_search_module(GST_VIDEO   REQUIRED gstreamer-video-1.0)
        pkg_search_module(GST_PBUTILS REQUIRED gstreamer-pbutils-1.0)
        pkg_search_module(GST_RTSP    REQUIRED gstreamer-rtsp-1.0)
        pkg_search_module(GST_CODECPARSERS REQUIRED gstreamer-codecparsers-1.0)

        # gst-rtsp-server might be in /app for Flatpak builds
        pkg_search_module(GST_RTSP_SERVER gstreamer-rtsp-server-1.0)
        if(NOT GST_RTSP_SERVER_FOUND AND DEFINED ENV{FLATPAK_ID})
            # Flatpak: manually configure gst-rtsp-server from /app
            # Check both lib64 and lib since meson may use either
            if(EXISTS "/app/lib64/libgstrtspserver-1.0.so")
                set(GST_RTSP_SERVER_FOUND TRUE)
                set(GST_RTSP_SERVER_INCLUDE_DIRS "/app/include/gstreamer-1.0")
                set(GST_RTSP_SERVER_LIBRARY_DIRS "/app/lib64")
                set(GST_RTSP_SERVER_LIBRARIES "gstrtspserver-1.0")
                message(STATUS "Found gst-rtsp-server in Flatpak /app/lib64 prefix")
            elseif(EXISTS "/app/lib/libgstrtspserver-1.0.so")
                set(GST_RTSP_SERVER_FOUND TRUE)
                set(GST_RTSP_SERVER_INCLUDE_DIRS "/app/include/gstreamer-1.0")
                set(GST_RTSP_SERVER_LIBRARY_DIRS "/app/lib")
                set(GST_RTSP_SERVER_LIBRARIES "gstrtspserver-1.0")
                message(STATUS "Found gst-rtsp-server in Flatpak /app/lib prefix")
            else()
                message(FATAL_ERROR "gstreamer-rtsp-server-1.0 not found in Flatpak /app")
            endif()
        elseif(NOT GST_RTSP_SERVER_FOUND)
            message(FATAL_ERROR "gstreamer-rtsp-server-1.0 not found")
        endif()

        add_library(gstreamer::gstreamer INTERFACE IMPORTED GLOBAL)

        target_include_directories(gstreamer::gstreamer INTERFACE
            ${GST_INCLUDE_DIRS} ${GST_BASE_INCLUDE_DIRS} ${GST_APP_INCLUDE_DIRS}
            ${GST_VIDEO_INCLUDE_DIRS} ${GST_PBUTILS_INCLUDE_DIRS} ${GST_RTSP_INCLUDE_DIRS}
            ${GST_RTSP_SERVER_INCLUDE_DIRS} ${GST_CODECPARSERS_INCLUDE_DIRS}
        )

        target_link_directories(gstreamer::gstreamer INTERFACE
            ${GST_LIBRARY_DIRS} ${GST_BASE_LIBRARY_DIRS} ${GST_APP_LIBRARY_DIRS}
            ${GST_VIDEO_LIBRARY_DIRS} ${GST_PBUTILS_LIBRARY_DIRS} ${GST_RTSP_LIBRARY_DIRS}
            ${GST_RTSP_SERVER_LIBRARY_DIRS}
        )

        target_link_libraries(gstreamer::gstreamer INTERFACE
            ${GST_LIBRARIES} ${GST_BASE_LIBRARIES} ${GST_APP_LIBRARIES}
            ${GST_VIDEO_LIBRARIES} ${GST_PBUTILS_LIBRARIES} ${GST_RTSP_LIBRARIES}
            ${GST_RTSP_SERVER_LIBRARIES} ${GST_CODECPARSERS_LIBRARIES}
        )

        message(STATUS "Using system GStreamer via pkg-config")

    else()
        message(FATAL_ERROR "GST_TOP_DIR must be set on Windows.")
    endif()
endif()

# ---- FFmpeg ----
if(NOT TARGET ffmpeg::ffmpeg)
    # Check for explicit path first (works on all platforms)
    if(DEFINED ENV{FFMPEG_TOP_DIR})
        set(FFMPEG_ROOT "$ENV{FFMPEG_TOP_DIR}")
        message(STATUS "Using FFmpeg from FFMPEG_TOP_DIR: ${FFMPEG_ROOT}")

        add_library(ffmpeg::ffmpeg INTERFACE IMPORTED GLOBAL)
        target_include_directories(ffmpeg::ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
        target_link_directories(ffmpeg::ffmpeg INTERFACE "${FFMPEG_ROOT}/lib")

        if(CMAKE_SYSTEM_NAME MATCHES "Windows")
            target_link_libraries(ffmpeg::ffmpeg INTERFACE
                avcodec.lib avformat.lib avutil.lib swscale.lib
            )
        else()
            # Linux/macOS with explicit path
            target_link_libraries(ffmpeg::ffmpeg INTERFACE
                avcodec avformat avutil swscale
            )
        endif()

    elseif(CMAKE_SYSTEM_NAME MATCHES "Linux|Darwin")
        # Fallback to system packages via pkg-config
        pkg_search_module(AVCODEC   REQUIRED libavcodec)
        pkg_search_module(AVFORMAT  REQUIRED libavformat)
        pkg_search_module(AVUTIL    REQUIRED libavutil)
        pkg_search_module(SWSCALE   REQUIRED libswscale)

        add_library(ffmpeg::ffmpeg INTERFACE IMPORTED GLOBAL)

        target_include_directories(ffmpeg::ffmpeg INTERFACE
            ${AVCODEC_INCLUDE_DIRS} ${AVFORMAT_INCLUDE_DIRS}
            ${AVUTIL_INCLUDE_DIRS} ${SWSCALE_INCLUDE_DIRS}
        )

        target_link_directories(ffmpeg::ffmpeg INTERFACE
            ${AVCODEC_LIBRARY_DIRS} ${AVFORMAT_LIBRARY_DIRS}
            ${AVUTIL_LIBRARY_DIRS} ${SWSCALE_LIBRARY_DIRS}
        )

        target_link_libraries(ffmpeg::ffmpeg INTERFACE
            ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES}
            ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES}
        )

        message(STATUS "Using system FFmpeg via pkg-config")

    else()
        message(FATAL_ERROR "FFMPEG_TOP_DIR must be set on Windows.")
    endif()
endif()

# ---- libuuid (Linux only actual dependency) ----
if(NOT TARGET uuid::uuid)
    if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        pkg_search_module(UUID REQUIRED uuid)

        add_library(uuid::uuid INTERFACE IMPORTED GLOBAL)
        target_include_directories(uuid::uuid INTERFACE ${UUID_INCLUDE_DIRS})
        target_link_directories(uuid::uuid INTERFACE ${UUID_LIBRARY_DIRS})
        target_link_libraries(uuid::uuid INTERFACE ${UUID_LIBRARIES})

    elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin|Windows")
        # Create a dummy no-op target for macOS/Windows compatibility
        add_library(uuid::uuid INTERFACE IMPORTED GLOBAL)
        # No includes or libs needed on macOS/Windows — uuid used differently
    endif()
endif()

# ---- NCNN (Optional - for neural network plugins) ----
set(NCNN_AVAILABLE FALSE)
if(NOT TARGET ncnn::ncnn)
    if(DEFINED ENV{NCNN_TOP_DIR})
        set(NCNN_ROOT "$ENV{NCNN_TOP_DIR}")
        set(NCNN_AVAILABLE TRUE)

        add_library(ncnn::ncnn INTERFACE IMPORTED GLOBAL)

        target_include_directories(ncnn::ncnn INTERFACE
            "${NCNN_ROOT}/include/ncnn"
        )

        if(CMAKE_SYSTEM_NAME MATCHES "Linux|Darwin")
            target_link_directories(ncnn::ncnn INTERFACE "${NCNN_ROOT}/lib")
            target_link_libraries(ncnn::ncnn INTERFACE ncnn pthread)
            # Only add gomp on Linux (OpenMP)
            if(CMAKE_SYSTEM_NAME MATCHES "Linux")
                target_link_libraries(ncnn::ncnn INTERFACE gomp)
            endif()
        elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
            target_link_directories(ncnn::ncnn INTERFACE
                $<$<CONFIG:Debug>:${NCNN_ROOT}/lib>
                $<$<CONFIG:Release>:${NCNN_ROOT}/lib>
                $<$<CONFIG:RelWithDebInfo>:${NCNN_ROOT}/lib>
                $<$<CONFIG:MinSizeRel>:${NCNN_ROOT}/lib>
            )
            target_link_libraries(ncnn::ncnn INTERFACE
                $<$<CONFIG:Debug>:ncnnd.lib>
                $<$<CONFIG:Release>:ncnn.lib>
                $<$<CONFIG:RelWithDebInfo>:ncnn.lib>
                $<$<CONFIG:MinSizeRel>:ncnn.lib>
            )
        endif()

        message(STATUS "NCNN found at: ${NCNN_ROOT}")

    # Flatpak build: check for NCNN at /app prefix
    elseif(DEFINED ENV{FLATPAK_ID} AND EXISTS "/app/include/ncnn/net.h")
        set(NCNN_ROOT "/app")
        set(NCNN_AVAILABLE TRUE)

        add_library(ncnn::ncnn INTERFACE IMPORTED GLOBAL)
        target_include_directories(ncnn::ncnn INTERFACE "${NCNN_ROOT}/include/ncnn")
        target_link_directories(ncnn::ncnn INTERFACE "${NCNN_ROOT}/lib")
        target_link_libraries(ncnn::ncnn INTERFACE ncnn pthread gomp)

        message(STATUS "NCNN found in Flatpak prefix: ${NCNN_ROOT}")

    else()
        message(STATUS "NCNN_TOP_DIR not set - NCNN plugins will not be built")
    endif()
endif()
