cmake_minimum_required(VERSION 3.14)
project(r_storage)

include(FetchContent)

FetchContent_Declare(
  nanots
  GIT_REPOSITORY https://github.com/dicroce/nanots.git
  GIT_TAG main
)
FetchContent_MakeAvailable(nanots)

# Install nanots shared library since it's a dependency (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS nanots
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()

file(GLOB R_STORAGE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/r_storage/*.h
)

file(GLOB R_STORAGE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_library(
    r_storage SHARED
    ${R_STORAGE_HEADERS}
    ${R_STORAGE_SOURCES}
)

target_include_directories(
    r_storage PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    r_storage PUBLIC
    r_db
    r_utils
    nanots
)

# Install the shared library (skip on macOS - bundled into .app)
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    install(TARGETS r_storage
        RUNTIME DESTINATION ${REVERE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${REVERE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${REVERE_INSTALL_LIBDIR}
    )
endif()

add_subdirectory(ut)
