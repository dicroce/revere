
add_library(
    r_onvif
    include/r_onvif/r_onvif_session.h
    source/r_onvif_session.cpp
)

target_include_directories(
    r_onvif PUBLIC
    include
    ../r_http/include
    ../r_utils/include
    ${LIBXML2_INCLUDE_DIRS}
)

target_link_directories(
    r_onvif PUBLIC
    ${LIBXML2_LIB_DIRS}
)

target_link_libraries(
    r_onvif
    r_http
    r_utils
    ${LIBXML2_LIBRARIES}
)
