
add_executable(
    r_http_ut
    include/framework.h
    source/framework.cpp
    include/test_r_http.h
    source/test_r_http.cpp
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
