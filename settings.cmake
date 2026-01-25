include(TestBigEndian)
set(CMAKE_CXX_STANDARD 17)

# Detect Flatpak build environment
if(DEFINED ENV{FLATPAK_ID})
    message(STATUS "Building for Flatpak: $ENV{FLATPAK_ID}")
    set(FLATPAK_BUILD TRUE)
else()
    set(FLATPAK_BUILD FALSE)
endif()

# Install destinations - flatpak uses standard FHS paths, portable uses single directory
if(FLATPAK_BUILD)
    set(REVERE_INSTALL_BINDIR bin)
    set(REVERE_INSTALL_LIBDIR lib)
else()
    set(REVERE_INSTALL_BINDIR revere)
    set(REVERE_INSTALL_LIBDIR revere)
endif()

if(IS_BIG_ENDIAN)
    add_compile_definitions(IS_BIG_ENDIAN)
else()
    add_compile_definitions(IS_LITTLE_ENDIAN)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Debug)
endif()

# Set RPATH for all targets to find libraries in the same directory
# Skip for Flatpak builds - they use standard library paths
if(NOT FLATPAK_BUILD)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

# Put all runtime outputs (DLLs and executables) in a common bin directory during build
# This ensures executables can find their DLL dependencies without manual copying
# This does NOT affect install destinations (controlled by install() commands)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# For multi-config generators (Visual Studio, Xcode) - outputs go to bin/Debug or bin/Release
foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG} CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/bin/${CONFIG})
endforeach()

# Apply build flags per platform and configuration
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    add_compile_definitions(IS_LINUX _GLIBCXX_ASSERTIONS)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -fPIC)
    add_link_options(-rdynamic)

    # Debug-specific flags (use generator expressions for consistency)
    add_compile_options(
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Debug>:-ggdb3>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-fasynchronous-unwind-tables>
    )
    add_link_options(
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Debug>:-ggdb3>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
        $<$<CONFIG:Debug>:-fasynchronous-unwind-tables>
    )

    # Release-specific flags
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-fno-math-errno>
        $<$<CONFIG:Release>:-fno-omit-frame-pointer>
    )
    add_link_options(
        $<$<CONFIG:Release>:-O3>
    )
    # -march=native breaks Flatpak portability (different CPUs)
    if(NOT FLATPAK_BUILD)
        add_compile_options($<$<CONFIG:Release>:-march=native>)
        add_link_options($<$<CONFIG:Release>:-march=native>)
    endif()

elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
    add_compile_definitions(IS_MACOS)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -fPIC)

    # Debug-specific flags
    add_compile_options(
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
    )
    add_link_options(
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
    )

    # Release-specific flags
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-fno-math-errno>
        $<$<CONFIG:Release>:-fno-omit-frame-pointer>
    )
    add_link_options(
        $<$<CONFIG:Release>:-O3>
    )

elseif(CMAKE_SYSTEM_NAME MATCHES "Windows")
    add_compile_definitions(IS_WINDOWS)
    add_compile_options(/W4 /MP /permissive- /Zc:preprocessor /wd4100)

    # Debug-specific flags (use generator expressions for multi-config generators like Visual Studio)
    add_compile_options(
        $<$<CONFIG:Debug>:/Od>                # disable optimizations
        $<$<CONFIG:Debug>:/Zi>                # debug info (PDB)
        $<$<CONFIG:Debug>:/RTC1>              # runtime checks (stack, uninit, etc.)
        $<$<CONFIG:Debug>:/Gy>                # function-level linking
        $<$<CONFIG:Debug>:/Zf>                # force inline debug info
    )
    add_link_options(
        $<$<CONFIG:Debug>:/DEBUG>
        $<$<CONFIG:Debug>:/INCREMENTAL>
    )

    # Release-specific flags
    add_compile_options(
        $<$<CONFIG:Release>:/O2>              # full optimization
        $<$<CONFIG:Release>:/Oi>              # intrinsic functions
        $<$<CONFIG:Release>:/GL>              # whole program optimization
        $<$<CONFIG:Release>:/Gy>              # function-level linking
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>            # link-time code generation
        $<$<CONFIG:Release>:/INCREMENTAL:NO>  # deterministic builds
    )
endif()
