
add_library(
    r_db
    include/r_db/r_sqlite_conn.h
    source/r_sqlite_conn.cpp
    include/r_db/r_sqlite_pager.h
    source/r_sqlite_pager.cpp
    include/sqlite3/sqlite3.h
    source/sqlite3.c
)

target_include_directories(
    r_db PUBLIC
    include
    ../r_utils/include/r_utils
)

target_link_libraries(
    r_db 
    r_utils
    ${CMAKE_DL_LIBS}
)
