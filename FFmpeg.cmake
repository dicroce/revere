
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    if(DEFINED ENV{FFMPEG_DEV_ROOT})
        set(
            FFMPEG_INCLUDE_DIRS
            $ENV{FFMPEG_DEV_ROOT}/include/
        )

        set(
            FFMPEG_LIB_DIRS
            $ENV{FFMPEG_DEV_ROOT}/lib
        )

        set(
            FFMPEG_LIBS
            swscale
            avdevice
            avformat
            avcodec
            avutil
        )
    else()
        message( FATAL_ERROR "Required FFMPEG_DEV_ROOT environment variable not set." )
    endif()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    find_package(PkgConfig)
    pkg_search_module(SWSCALE REQUIRED libswscale) 
    pkg_search_module(AVDEVICE REQUIRED libavdevice) 
    pkg_search_module(AVFORMAT REQUIRED libavformat) 
    pkg_search_module(AVCODEC REQUIRED libavcodec) 
    pkg_search_module(AVUTIL REQUIRED libavutil) 

    set(
        FFMPEG_INCLUDE_DIRS
        ${SWSCALE_INCLUDE_DIRS}
        ${AVDEVICE_INCLUDE_DIRS}
        ${AVFORMAT_INCLUDE_DIRS}
        ${AVCODEC_INCLUDE_DIRS}
        ${AVUTIL_INCLUDE_DIRS}
    )

    set(
        FFMPEG_LIB_DIRS
        ${SWSCALE_LIB_DIRS}
        ${AVDEVICE_LIB_DIRS}
        ${AVFORMAT_LIB_DIRS}
        ${AVCODEC_LIB_DIRS}
        ${AVUTIL_LIB_DIRS}
    )

    set(
        FFMPEG_LIBS
        ${SWSCALE_LIBRARIES}
        ${AVDEVICE_LIBRARIES}
        ${AVFORMAT_LIBRARIES}
        ${AVCODEC_LIBRARIES}
        ${AVUTIL_LIBRARIES}
    )

endif()
