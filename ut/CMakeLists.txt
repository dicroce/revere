add_executable(
    revere_ut
    include/bad_guy.h
    include/framework.h
    source/framework.cpp
    include/utils.h
    source/utils.cpp
    include/test_r_utils.h
    source/test_r_utils.cpp
    include/test_r_http.h
    source/test_r_http.cpp
    include/test_r_db.h
    source/test_r_db.cpp
    include/test_r_pipeline.h
    source/test_r_pipeline.cpp
    include/test_r_mux.h
    source/test_r_mux.cpp
    include/test_r_storage.h
    source/test_r_storage.cpp
    include/test_r_disco.h
    source/test_r_disco.cpp
    include/test_r_vss.h
    source/test_r_vss.cpp
    include/test_r_onvif.h
    source/test_r_onvif.cpp
    include/test_r_codec.h
    source/test_r_codec.cpp
    include/test_r_motion.h
    source/test_r_motion.cpp
)

if(MSVC)
    target_link_options(revere_ut PUBLIC "/STACK:10000000")
endif()

target_include_directories(
    revere_ut PUBLIC 
    include
    ../r_utils/include
    ../r_http/include
    ../r_db/include
    ../r_pipeline/include
    ../r_fakey/include
    ../r_mux/include
    ../r_storage/include
    ../r_disco/include
    ../r_vss/include
    ../r_onvif/include
    ../r_codec/include
    ../r_motion/include
    ${GSTREAMER_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)
target_link_directories(
    revere_ut PUBLIC
    ${GST_LIBRARY_DIRS}
    ${FFMPEG_LIB_DIRS}
)
target_link_libraries(
    revere_ut LINK_PUBLIC 
    r_motion
    r_http
    r_db
    r_utils
    r_pipeline
    r_fakey
    r_mux
    r_storage
    r_disco
    r_vss
    r_onvif
    r_codec
    r_motion
    gstcodecparsers-1.0
    ${GST_LIBRARIES}
    ${FFMPEG_LIBS}
)
if(CMAKE_SYSTEM MATCHES "Linux-")
    target_link_libraries(
        revere_ut PUBLIC
        pthread
        dl
        uuid
    )
elseif(CMAKE_SYSTEM MATCHES "Windows")
endif(CMAKE_SYSTEM MATCHES "Linux-")
