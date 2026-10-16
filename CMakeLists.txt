cmake_minimum_required(VERSION 3.14)

# Version and build number from git, fixed when CMake configures:
#   REVERE_VERSION_STRING  git describe without the leading v: "0.0.31" on a
#                          release tag, "0.0.31-4-gabc1234" four commits after
#                          it, the short hash when there are no tags
#   REVERE_BUILD_NUMBER    commits up to HEAD (git rev-list --count)
#   REVERE_GIT_REVISION    short hash
# The build time and whether the tree has uncommitted changes ("-dirty") are
# stamped on every build instead (libs/r_vss/build_info.cmake), so rebuilding
# the same source still shows a different build.
find_package(Git QUIET)
set(REVERE_GIT_REVISION "unknown")
set(REVERE_GIT_DESCRIBE "unknown")
set(REVERE_VERSION_STRING "unknown")
set(REVERE_BUILD_NUMBER 0)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _git_revision
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _git_result
    )
    if(_git_result EQUAL 0 AND _git_revision)
        set(REVERE_GIT_REVISION "${_git_revision}")

        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --match "v[0-9]*" --always
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE REVERE_GIT_DESCRIBE
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        string(REGEX REPLACE "^v([0-9])" "\\1" REVERE_VERSION_STRING "${REVERE_GIT_DESCRIBE}")

        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE _git_count
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_git_count MATCHES "^[0-9]+$")
            set(REVERE_BUILD_NUMBER ${_git_count})
        endif()

        # Configure again when HEAD moves (a commit, checkout or new tag), so
        # the values above never describe an older commit than the one built.
        foreach(_git_path HEAD packed-refs refs/tags)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --path-format=absolute --git-path ${_git_path}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE _git_file
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_git_file AND EXISTS "${_git_file}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_git_file}")
            endif()
        endforeach()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} symbolic-ref -q HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE _git_branch_ref
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_git_branch_ref)
            execute_process(
                COMMAND ${GIT_EXECUTABLE} rev-parse --path-format=absolute --git-path ${_git_branch_ref}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE _git_file
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(_git_file AND EXISTS "${_git_file}")
                set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_git_file}")
            endif()
        endif()
    endif()
endif()

message(STATUS "Revere version: ${REVERE_VERSION_STRING} (build ${REVERE_BUILD_NUMBER})")
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
