
add_executable(
    r_http_ut
    include/framework.h
    source/framework.cpp
    include/test_r_http.h
    source/test_r_http.cpp
    include/test_r_websocket.h
    source/test_r_websocket.cpp
)

target_include_directories(
    r_http_ut PUBLIC 
    include
)

target_link_libraries(
    r_http_ut LINK_PUBLIC
    r_http
    r_utils
    platform::platform
)

# Ensure this is built as a console application on Windows
if(WIN32)
    set_target_properties(r_http_ut PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:CONSOLE"
    )
endif()
