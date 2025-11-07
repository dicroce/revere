
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
    endif()
endif()

# ---- OpenCV ----
if(NOT TARGET opencv::opencv)
    if(CMAKE_SYSTEM_NAME MATCHES "Linux|Darwin")
        pkg_search_module(OPENCV opencv4)

        if(OPENCV_FOUND)
            add_library(opencv::opencv INTERFACE IMPORTED GLOBAL)
            target_include_directories(opencv::opencv INTERFACE ${OPENCV_INCLUDE_DIRS})
            target_link_directories(opencv::opencv INTERFACE ${OPENCV_LIBRARY_DIRS})
            target_link_libraries(opencv::opencv INTERFACE ${OPENCV_LIBRARIES})
        else()
            message(WARNING "OpenCV not found - motion detection features will be limited")
            # Create a dummy target for compatibility
            add_library(opencv::opencv INTERFACE IMPORTED GLOBAL)
        endif()

    elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
        if(NOT DEFINED ENV{OPENCV_TOP_DIR})
            message(FATAL_ERROR "OPENCV_TOP_DIR must be set on Windows.")
        endif()

        set(OPENCV_ROOT "$ENV{OPENCV_TOP_DIR}")

        add_library(opencv::opencv INTERFACE IMPORTED GLOBAL)
        # Define OPENCV_STATIC for static linking if you're using static libraries
        target_compile_definitions(opencv::opencv INTERFACE
            OPENCV_STATIC
        )

        # Base include dir
        target_include_directories(opencv::opencv INTERFACE
            "${OPENCV_ROOT}/include"
        )
        
        # Set different lib directories for Debug and Release configurations
        target_link_directories(opencv::opencv INTERFACE
            $<$<CONFIG:Debug>:${OPENCV_ROOT}/x64/vc17/lib>
            $<$<CONFIG:Release>:${OPENCV_ROOT}/x64/vc17/lib>
            $<$<CONFIG:RelWithDebInfo>:${OPENCV_ROOT}/x64/vc17/lib>
            $<$<CONFIG:MinSizeRel>:${OPENCV_ROOT}/x64/vc17/lib>
        )

        # Use generator expressions to link against debug or release libraries based on the build configuration
        set(OPENCV_MODULES
            opencv_bgsegm
            opencv_calib3d
            opencv_core
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

        foreach(module ${OPENCV_MODULES})
            target_link_libraries(opencv::opencv INTERFACE
                $<$<CONFIG:Debug>:${module}4120d.lib>
                $<$<CONFIG:Release>:${module}4120.lib>
                $<$<CONFIG:RelWithDebInfo>:${module}4120.lib>
                $<$<CONFIG:MinSizeRel>:${module}4120.lib>
            )
        endforeach()
        
        # Add dependent Windows libraries often needed by OpenCV
        target_link_libraries(opencv::opencv INTERFACE
            vfw32.lib
            comctl32.lib
        )
    endif()
endif()

# ---- GStreamer ----
if(NOT TARGET gstreamer::gstreamer)
    if(CMAKE_SYSTEM_NAME MATCHES "Linux|Darwin")
        pkg_search_module(GST         REQUIRED gstreamer-1.0)
        pkg_search_module(GST_BASE    REQUIRED gstreamer-base-1.0)
        pkg_search_module(GST_APP     REQUIRED gstreamer-app-1.0)
        pkg_search_module(GST_VIDEO   REQUIRED gstreamer-video-1.0)
        pkg_search_module(GST_PBUTILS REQUIRED gstreamer-pbutils-1.0)
        pkg_search_module(GST_RTSP    REQUIRED gstreamer-rtsp-1.0)
        pkg_search_module(GST_RTSP_SERVER REQUIRED gstreamer-rtsp-server-1.0)
        pkg_search_module(GST_CODECPARSERS REQUIRED gstreamer-codecparsers-1.0)

        add_library(gstreamer::gstreamer INTERFACE IMPORTED GLOBAL)

        target_include_directories(gstreamer::gstreamer INTERFACE
            ${GST_INCLUDE_DIRS}
            ${GST_BASE_INCLUDE_DIRS}
            ${GST_APP_INCLUDE_DIRS}
            ${GST_VIDEO_INCLUDE_DIRS}
            ${GST_PBUTILS_INCLUDE_DIRS}
            ${GST_RTSP_INCLUDE_DIRS}
            ${GST_RTSP_SERVER_INCLUDE_DIRS}
            ${GST_CODECPARSERS_INCLUDE_DIRS}
        )

        target_link_directories(gstreamer::gstreamer INTERFACE
            ${GST_LIBRARY_DIRS}
            ${GST_BASE_LIBRARY_DIRS}
            ${GST_APP_LIBRARY_DIRS}
            ${GST_VIDEO_LIBRARY_DIRS}
            ${GST_PBUTILS_LIBRARY_DIRS}
            ${GST_RTSP_LIBRARY_DIRS}
            ${GST_RTSP_SERVER_LIBRARY_DIRS}
        )

        target_link_libraries(gstreamer::gstreamer INTERFACE
            ${GST_LIBRARIES}
            ${GST_BASE_LIBRARIES}
            ${GST_APP_LIBRARIES}
            ${GST_VIDEO_LIBRARIES}
            ${GST_PBUTILS_LIBRARIES}
            ${GST_RTSP_LIBRARIES}
            ${GST_RTSP_SERVER_LIBRARIES}
            ${GST_CODECPARSERS_LIBRARIES}
        )

        message(STATUS "GStreamer libs: ${GST_LIBRARIES} ${GST_BASE_LIBRARIES} ${GST_APP_LIBRARIES} ${GST_VIDEO_LIBRARIES} ${GST_PBUTILS_LIBRARIES} ${GST_RTSP_LIBRARIES} ${GST_RTSP_SERVER_LIBRARIES} ${GST_CODECPARSERS_LIBRARIES}")

    elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
        if(NOT DEFINED ENV{GST_TOP_DIR})
            message(FATAL_ERROR "GST_TOP_DIR must be set on Windows.")
        endif()
        set(GST_ROOT "$ENV{GST_TOP_DIR}")

        add_library(gstreamer::gstreamer INTERFACE IMPORTED GLOBAL)
        target_include_directories(gstreamer::gstreamer INTERFACE
            "${GST_ROOT}/include/gstreamer-1.0"
            "${GST_ROOT}/include/glib-2.0"
            "${GST_ROOT}/lib/glib-2.0/include"
        )
        target_link_directories(gstreamer::gstreamer INTERFACE "${GST_ROOT}/lib")

        target_link_libraries(gstreamer::gstreamer INTERFACE
            gstreamer-1.0.lib
            gstbase-1.0.lib
            gstapp-1.0.lib
            gstvideo-1.0.lib
            gstpbutils-1.0.lib
            gstrtsp-1.0.lib
            gstrtspserver-1.0.lib
            gstcodecparsers-1.0.lib
            gstsdp-1.0.lib
            glib-2.0.lib
            gobject-2.0.lib
        )
    endif()
endif()

# ---- FFmpeg ----
if(NOT TARGET ffmpeg::ffmpeg)
    if(CMAKE_SYSTEM_NAME MATCHES "Linux|Darwin")
        pkg_search_module(AVCODEC   REQUIRED libavcodec)
        pkg_search_module(AVFORMAT  REQUIRED libavformat)
        pkg_search_module(AVUTIL    REQUIRED libavutil)
        pkg_search_module(SWSCALE   REQUIRED libswscale)

        add_library(ffmpeg::ffmpeg INTERFACE IMPORTED GLOBAL)

        target_include_directories(ffmpeg::ffmpeg INTERFACE
            ${AVCODEC_INCLUDE_DIRS}
            ${AVFORMAT_INCLUDE_DIRS}
            ${AVUTIL_INCLUDE_DIRS}
            ${SWSCALE_INCLUDE_DIRS}
        )

        target_link_directories(ffmpeg::ffmpeg INTERFACE
            ${AVCODEC_LIBRARY_DIRS}
            ${AVFORMAT_LIBRARY_DIRS}
            ${AVUTIL_LIBRARY_DIRS}
            ${SWSCALE_LIBRARY_DIRS}
        )

        target_link_libraries(ffmpeg::ffmpeg INTERFACE
            ${AVCODEC_LIBRARIES}
            ${AVFORMAT_LIBRARIES}
            ${AVUTIL_LIBRARIES}
            ${SWSCALE_LIBRARIES}
        )

        message(STATUS "FFmpeg libs: ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES}")

    elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
        if(NOT DEFINED ENV{FFMPEG_TOP_DIR})
            message(FATAL_ERROR "FFMPEG_TOP_DIR must be set on Windows.")
        endif()
        set(FFMPEG_ROOT "$ENV{FFMPEG_TOP_DIR}")

        add_library(ffmpeg::ffmpeg INTERFACE IMPORTED GLOBAL)
        target_include_directories(ffmpeg::ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
        target_link_directories(ffmpeg::ffmpeg INTERFACE "${FFMPEG_ROOT}/lib")
        target_link_libraries(ffmpeg::ffmpeg INTERFACE
            avcodec.lib
            avformat.lib
            avutil.lib
            swscale.lib
        )
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
        
        # Include directories
        target_include_directories(ncnn::ncnn INTERFACE 
            "${NCNN_ROOT}/include/ncnn"
        )
        
        # Library directories
        if(CMAKE_SYSTEM_NAME MATCHES "Linux|Darwin")
            target_link_directories(ncnn::ncnn INTERFACE "${NCNN_ROOT}/lib")
            target_link_libraries(ncnn::ncnn INTERFACE
                ncnn
                pthread
            )
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
    else()
        message(STATUS "NCNN_TOP_DIR not set - NCNN plugins will not be built")
    endif()
endif()
