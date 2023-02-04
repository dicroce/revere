
if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    if(DEFINED ENV{GSTREAMER_DEV_ROOT})
        set(
            LIBXML2_INCLUDE_DIRS
            $ENV{GSTREAMER_DEV_ROOT}/include/libxml2
            $ENV{GSTREAMER_DEV_ROOT}/include
        )
        
        set(
            LIBXML2_LIB_DIRS
            $ENV{GSTREAMER_DEV_ROOT}/lib
        )
        
        set(
            LIBXML2_LIBRARIES
            xml2.lib
        )    
    else()
        message( FATAL_ERROR "Required GSTREAMER_DEV_ROOT environment variable not set." )
    endif()
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

    find_package(LibXml2)

endif()
