
add_library(
    r_http
    include/r_http/r_client_request.h
    source/r_client_request.cpp
    include/r_http/r_client_response.h
    source/r_client_response.cpp
    include/r_http/r_http_exception.h
    source/r_http_exception.cpp
    include/r_http/r_methods.h
    source/r_methods.cpp
    include/r_http/r_range_request_generator.h
    source/r_range_request_generator.cpp
    include/r_http/r_server_request.h
    source/r_server_request.cpp
    include/r_http/r_server_response.h
    source/r_server_response.cpp
    include/r_http/r_uri.h
    source/r_uri.cpp
    include/r_http/r_utils.h
    source/r_utils.cpp
    include/r_http/r_web_server.h
    source/r_web_server.cpp
)

target_include_directories(
    r_http PUBLIC 
    include
    ../r_utils/include
)

target_link_libraries(
    r_http 
    r_utils
)
