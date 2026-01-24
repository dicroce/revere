cmake_minimum_required(VERSION 3.14)

# Get version from git tags, fall back to hash if no tags
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE REVERE_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE REVERE_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Check if HEAD is exactly on a version tag (no commits after the tag)
    # git describe --tags --exact-match only succeeds if HEAD is directly on a tag
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match --match "v[0-9]*"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_EXACT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_EXACT_TAG_RESULT
    )

    if(GIT_EXACT_TAG_RESULT EQUAL 0 AND GIT_EXACT_TAG MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)")
        # HEAD is exactly on a version tag - use the tag version
        string(REGEX REPLACE "^v" "" REVERE_VERSION_STRING "${GIT_EXACT_TAG}")
    else()
        # HEAD is not on a tag - use git hash as version
        set(REVERE_VERSION_STRING "${REVERE_GIT_REVISION}")
    endif()
else()
    set(REVERE_GIT_REVISION "unknown")
    set(REVERE_GIT_DESCRIBE "unknown")
    set(REVERE_VERSION_STRING "unknown")
endif()

message(STATUS "Revere version: ${REVERE_VERSION_STRING}")
message(STATUS "Revere git revision: ${REVERE_GIT_REVISION}")
message(STATUS "Revere git describe: ${REVERE_GIT_DESCRIBE}")

if(APPLE)
    project(revere LANGUAGES C CXX OBJC OBJCXX)
else()
    project(revere)
endif()

include(settings.cmake)
include(dependencies.cmake)

add_subdirectory(libs)
add_subdirectory(apps)

# External plugin support
set(EXTERNAL_PLUGIN_REPOS "" CACHE STRING "Semicolon-separated list of external plugin git repository URLs or local directories")

if(EXTERNAL_PLUGIN_REPOS)
    include(FetchContent)

    string(REPLACE ";" "\n" PLUGIN_LIST "${EXTERNAL_PLUGIN_REPOS}")
    message(STATUS "Loading external plugins:\n${PLUGIN_LIST}")

    foreach(PLUGIN_URL ${EXTERNAL_PLUGIN_REPOS})
        # Check if this is a local directory or a git URL
        if(IS_DIRECTORY "${PLUGIN_URL}")
            # Local directory - extract name from path
            get_filename_component(PLUGIN_NAME "${PLUGIN_URL}" NAME)
            message(STATUS "Loading local plugin: ${PLUGIN_NAME} from ${PLUGIN_URL}")

            FetchContent_Declare(
                ${PLUGIN_NAME}
                SOURCE_DIR ${PLUGIN_URL}
            )
        else()
            # Git repository - extract plugin name from URL
            # Handles both https://github.com/user/repo.git and git@github.com:user/repo.git
            string(REGEX REPLACE ".*[/:](.+)\\.git$" "\\1" PLUGIN_NAME "${PLUGIN_URL}")
            string(REGEX REPLACE ".*[/:](.+)$" "\\1" PLUGIN_NAME "${PLUGIN_NAME}")

            message(STATUS "Fetching external plugin: ${PLUGIN_NAME} from ${PLUGIN_URL}")

            FetchContent_Declare(
                ${PLUGIN_NAME}
                GIT_REPOSITORY ${PLUGIN_URL}
                GIT_TAG main
            )
        endif()

        FetchContent_MakeAvailable(${PLUGIN_NAME})
    endforeach()
else()
    message(STATUS "No external plugins configured (use -DEXTERNAL_PLUGIN_REPOS to add them)")
endif()

# ---- CPack Configuration for macOS DMG ----
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Revere")
    set(CPACK_DMG_FORMAT "UDBZ")  # Compressed, read-only
    set(CPACK_PACKAGE_FILE_NAME "Revere-${REVERE_VERSION_STRING}-macOS")
    set(CPACK_PACKAGE_VENDOR "Revere")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Revere Video Surveillance System")

    include(CPack)
endif()
