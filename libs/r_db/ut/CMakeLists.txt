
add_executable(
    r_db_ut
    include/framework.h
    source/framework.cpp
    include/test_r_db.h
    source/test_r_db.cpp
)

target_include_directories(
    r_db_ut PUBLIC 
    include
)

target_link_libraries(
    r_db_ut LINK_PUBLIC 
    r_db
    r_utils
)
