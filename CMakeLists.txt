cmake_minimum_required(VERSION 3.14)

if(APPLE)
    project(revere VERSION 0.0.1 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(revere VERSION 0.0.1)
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
