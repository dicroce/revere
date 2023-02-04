
add_library(
    r_storage
    include/r_storage/r_dumbdex.h
    source/r_dumbdex.cpp
    include/r_storage/r_ind_block.h
    source/r_ind_block.cpp
    include/r_storage/r_rel_block.h
    source/r_rel_block.cpp
    include/r_storage/r_storage_file.h
    source/r_storage_file.cpp
    include/r_storage/r_storage_file_reader.h
    source/r_storage_file_reader.cpp
    include/r_storage/r_ring.h
    source/r_ring.cpp
)

target_include_directories(
    r_storage PUBLIC
    include
    ../r_utils/include
    ../r_db/include
)

target_link_libraries(
    r_storage
    r_utils
)
