cmake_minimum_required(VERSION 3.14)
project(revere_mcp)

file(GLOB REVERE_MCP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp
)

add_executable(revere_mcp ${REVERE_MCP_SOURCES})

target_link_libraries(
    revere_mcp
    r_http
    r_utils
)

install(
    TARGETS revere_mcp
    RUNTIME DESTINATION ${REVERE_INSTALL_BINDIR}
)
