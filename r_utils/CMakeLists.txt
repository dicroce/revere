
add_library(
    r_utils
    include/r_utils/r_actor.h
    include/r_utils/r_macro.h
    include/r_utils/r_args.h
    source/r_args.cpp
    include/r_utils/r_file_lock.h
    source/r_file_lock.cpp
    include/r_utils/r_logger.h
    source/r_logger.cpp
    include/r_utils/r_string_utils.h
    source/r_string_utils.cpp
    include/r_utils/r_file.h
    source/r_file.cpp
    include/r_utils/r_exception.h
    source/r_exception.cpp
    include/r_utils/r_stack_trace.h
    source/r_stack_trace.cpp
    include/r_utils/r_socket_address.h
    source/r_socket_address.cpp
    include/r_utils/interfaces/r_pollable.h
    include/r_utils/interfaces/r_socket_io.h
    include/r_utils/interfaces/r_stream_io.h
    include/r_utils/r_socket.h
    source/r_socket.cpp
    include/r_utils/r_md5.h
    source/r_md5.cpp
    include/r_utils/r_time_utils.h
    source/r_time_utils.cpp
    include/r_utils/r_udp_sender.h
    source/r_udp_sender.cpp
    include/r_utils/r_udp_receiver.h
    source/r_udp_receiver.cpp
    include/r_utils/r_byte_ptr.h
    source/r_byte_ptr.cpp
    include/r_utils/r_algorithms.h
    source/r_algorithms.cpp
    include/r_utils/r_uuid.h
    source/r_uuid.cpp
    include/r_utils/r_nullable.h
    source/r_nullable.cpp
    include/r_utils/r_blob_tree.h
    source/r_blob_tree.cpp
    include/r_utils/r_std_utils.h
    source/r_std_utils.cpp
    include/r_utils/r_memory_map.h
    source/r_memory_map.cpp
    include/r_utils/r_timer.h
    source/r_timer.cpp
    include/r_utils/r_sha1.h
    source/r_sha1.cpp
    include/r_utils/r_udp_socket.h
    source/r_udp_socket.cpp
    include/r_utils/r_process.h
    source/r_process.cpp
    include/r_utils/r_blocking_q.h
    include/r_utils/r_work_q.h
    include/r_utils/r_server_threaded.h
    include/r_utils/r_functional.h
    include/r_utils/r_pool.h
    include/r_utils/r_avg.h
)
target_include_directories(
    r_utils PUBLIC
    include
)
if(CMAKE_SYSTEM MATCHES "Linux-")
    include(FindLibunwind.cmake)

    target_link_libraries(
        r_utils PUBLIC
        uuid
        unwind
    )
elseif(CMAKE_SYSTEM MATCHES "Windows")
    target_link_libraries(
        r_utils PUBLIC
        DbgHelp 
        ws2_32 
        iphlpapi 
        rpcrt4.lib
    )
endif(CMAKE_SYSTEM MATCHES "Linux-")
