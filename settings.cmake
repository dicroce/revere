include(TestBigEndian)
set(CMAKE_CXX_STANDARD 17)

# Detect Flatpak build environment
if(DEFINED ENV{FLATPAK_ID})
    message(STATUS "Building for Flatpak: $ENV{FLATPAK_ID}")
    set(FLATPAK_BUILD TRUE)
else()
    set(FLATPAK_BUILD FALSE)
endif()

# Detect Snap build environment (CRAFT_PART_BUILD is set during snapcraft builds)
if(DEFINED ENV{CRAFT_PART_BUILD} OR SNAP_BUILD)
    message(STATUS "Building for Snap")
    set(SNAP_BUILD TRUE)
else()
    set(SNAP_BUILD FALSE)
endif()

# Generic flag for any sandboxed/packaged build that uses FHS paths
set(PACKAGED_BUILD FALSE)
if(FLATPAK_BUILD OR SNAP_BUILD)
    set(PACKAGED_BUILD TRUE)
endif()

# Portable build (e.g. AppImage): keep the normal single-directory install layout
# (so relative model/plugin/webui lookups stay consistent) but drop -march=native
# so the binary runs on CPUs other than the build machine. Default OFF leaves
# local/dev builds untouched; CI passes -DREVERE_PORTABLE_BUILD=ON.
option(REVERE_PORTABLE_BUILD "Build portable binaries (no -march=native) for redistribution" OFF)

# Install destinations - packaged builds (Flatpak/Snap) use standard FHS paths, portable uses single directory
if(PACKAGED_BUILD)
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
# Skip for packaged builds (Flatpak/Snap) - they use standard library paths
if(NOT PACKAGED_BUILD)
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
    # -march=native breaks portability (different CPUs): skip it for packaged
    # (Flatpak/Snap) and portable (AppImage) builds meant to run elsewhere.
    if(NOT PACKAGED_BUILD AND NOT REVERE_PORTABLE_BUILD)
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
        $<$<CONFIG:Release>:/Zi>              # PDBs — see below
    )
    # Release ships with symbols. Without /Zi + /DEBUG a crash dump resolves to
    # nothing but module+offset, which is how a run of heap-corruption crashes
    # (0xc0000374) went undiagnosed: every WER report bucketed as
    # "PCH_*_FROM_ntdll" with no frame we could attribute.
    #
    # /DEBUG implicitly turns OFF /OPT:REF and /OPT:ICF, so restore them
    # explicitly — otherwise the binary we debug is not the binary we shipped.
    # The PDBs land next to the binaries and are NOT installed (no install rule
    # references them), so the packaged installer is unaffected.
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>            # link-time code generation
        $<$<CONFIG:Release>:/INCREMENTAL:NO>  # deterministic builds
        $<$<CONFIG:Release>:/DEBUG>           # emit PDB
        $<$<CONFIG:Release>:/OPT:REF>         # restore what /DEBUG disabled
        $<$<CONFIG:Release>:/OPT:ICF>         # restore what /DEBUG disabled
    )
endif()
