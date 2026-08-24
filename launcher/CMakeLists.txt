# Super-build for the custom Nintendont launcher.
#
# The launcher spans three toolchains (bare devkitPPC, big-endian devkitARM, and
# devkitPPC+libogc), and CMake binds one compiler per build tree, so each toolchain
# group is configured as its own ExternalProject with its own toolchain file. The
# groups are wired ppc -> arm -> wii and share three output dirs (generated
# headers, data blobs, the fatfs-ppc lib). The final boot.dol is packaged into the
# HBC app zip.
#
# Nothing here is version-specific: the mod ships as mod_<region>.bin files
# packaged alongside boot.dol and read at runtime. Normally driven by the root
# project's `launcher` target (which passes those bins in), but also configurable
# standalone for launcher dev, in which case the app is packaged without them.

cmake_minimum_required(VERSION 3.21)
project(susamune_launcher NONE)

include(ExternalProject)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/BundledToolchain.cmake")

# Resolve (and, if needed, extract) the devkitPro toolchains once here, then pass
# the absolute paths to every group so they don't each re-resolve/extract.
susamune_resolve_devkitpro()

# --- Inputs (overridable; defaults keep a standalone configure working) --------
if(NOT LAUNCHER_PYTHON)
    find_program(LAUNCHER_PYTHON NAMES python3 python REQUIRED)
endif()
get_filename_component(_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(LAUNCHER_SCRIPTS_DIR "${_repo_root}/scripts" CACHE PATH "repo scripts/ dir")
set(LAUNCHER_DEFAULT_SOURCE "di" CACHE STRING "meta.xml default load source (di/sd/usb)")
set(LAUNCHER_VERSION "" CACHE STRING "meta.xml version override")
set(LAUNCHER_TEST_LOG "" CACHE FILEPATH "tester log included in the HBC app")
set(LAUNCHER_PATTERN_TEST_LOG "" CACHE FILEPATH "optional pattern tester log")
set(LAUNCHER_CHANGELOG "" CACHE FILEPATH "changelog included in the HBC app")
option(LAUNCHER_DISABLE_SUSAMUNE_PATCH "Disable the Susamune patch in the launcher kernel" OFF)
option(SUSAMUNE_MINIMAL_FREETYPE
    "Register only the FreeType modules used by the embedded TrueType font" ON)
set(SUSAMUNE_LOADER_OPTIMIZATION "-Os" CACHE STRING
    "PPC loader optimization (default -Os; -O3 is the speed override)")
set_property(CACHE SUSAMUNE_LOADER_OPTIMIZATION PROPERTY STRINGS "-O3" "-Os")

# mod_<region>.bin files to ship inside the HBC app dir. Empty for a standalone
# launcher build: the kernel then finds no staged blob at boot and runs the game
# unmodified, which is exactly what stock Nintendont does.
set(SUSAMUNE_MOD_BINS "" CACHE STRING "mod_<region>.bin files to package")

# --- Shared output locations, produced by one group and consumed by others -----
set(_shared    "${CMAKE_BINARY_DIR}/shared")
set(_data_dir  "${_shared}/data")
set(_gen_dir   "${_shared}/gen")
set(_lib_dir   "${_shared}/lib")
set(_boot_dol  "${_shared}/boot.dol")
file(MAKE_DIRECTORY "${_data_dir}" "${_gen_dir}" "${_lib_dir}")
if(NOT EXISTS "${_gen_dir}/susamune_build_id.h")
    file(WRITE "${_gen_dir}/susamune_build_id.h"
        "#ifndef SUSAMUNE_BUILD_CHECKSUM\n#define SUSAMUNE_BUILD_CHECKSUM \"unknown\"\n#endif\n")
endif()

set(_common_args
    "-DCMAKE_BUILD_TYPE="
    "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
    "-DDEVKITPPC=${DEVKITPPC}"
    "-DDEVKITARM=${DEVKITARM}"
    "-DLAUNCHER_PYTHON=${LAUNCHER_PYTHON}"
    "-DLAUNCHER_SCRIPTS_DIR=${LAUNCHER_SCRIPTS_DIR}"
    "-DLAUNCHER_DATA_DIR=${_data_dir}"
    "-DLAUNCHER_GEN_DIR=${_gen_dir}"
    "-DLAUNCHER_LIB_DIR=${_lib_dir}")

set(_cmake_dir "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

ExternalProject_Add(ppc
    SOURCE_DIR "${_cmake_dir}/group-ppc"
    BINARY_DIR "${CMAKE_BINARY_DIR}/group-ppc"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${_cmake_dir}/toolchain-devkitPPC.cmake"
        ${_common_args}
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON)

ExternalProject_Add(arm
    SOURCE_DIR "${_cmake_dir}/group-arm"
    BINARY_DIR "${CMAKE_BINARY_DIR}/group-arm"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${_cmake_dir}/toolchain-devkitARM.cmake"
        "-DLAUNCHER_DISABLE_SUSAMUNE_PATCH=${LAUNCHER_DISABLE_SUSAMUNE_PATCH}"
        ${_common_args}
    DEPENDS ppc
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON)

ExternalProject_Add(wii
    SOURCE_DIR "${_cmake_dir}/group-wii"
    BINARY_DIR "${CMAKE_BINARY_DIR}/group-wii"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${_cmake_dir}/toolchain-wii.cmake"
        "-DLAUNCHER_BOOT_DOL=${_boot_dol}"
        "-DSUSAMUNE_MINIMAL_FREETYPE=${SUSAMUNE_MINIMAL_FREETYPE}"
        "-DSUSAMUNE_LOADER_OPTIMIZATION:STRING=${SUSAMUNE_LOADER_OPTIMIZATION}"
        ${_common_args}
    DEPENDS ppc arm
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON)

# --- Package boot.dol + the mod bins into the HBC app zip ---------------------
if(LAUNCHER_OUTPUT_ZIP)
    set(_mod_bin_args "")
    if(SUSAMUNE_MOD_BINS)
        set(_mod_bin_args --mod-bins ${SUSAMUNE_MOD_BINS})
    endif()
    set(_version_args "")
    if(LAUNCHER_VERSION)
        set(_version_args --version "${LAUNCHER_VERSION}")
    endif()
    set(_test_log_args "")
    if(LAUNCHER_TEST_LOG)
        set(_test_log_args --test-log "${LAUNCHER_TEST_LOG}")
    endif()
    set(_pattern_test_log_args "")
    if(LAUNCHER_PATTERN_TEST_LOG)
        set(_pattern_test_log_args --pattern-test-log "${LAUNCHER_PATTERN_TEST_LOG}")
    endif()
    set(_changelog_args "")
    if(LAUNCHER_CHANGELOG)
        set(_changelog_args --changelog "${LAUNCHER_CHANGELOG}")
    endif()
    add_custom_target(package ALL
        COMMAND "${LAUNCHER_PYTHON}" "${LAUNCHER_SCRIPTS_DIR}/package_launcher.py"
            --boot-dol "${_boot_dol}"
            --out-zip  "${LAUNCHER_OUTPUT_ZIP}"
            --source   "${LAUNCHER_DEFAULT_SOURCE}"
            ${_version_args}
            ${_test_log_args}
            ${_pattern_test_log_args}
            ${_changelog_args}
            ${_mod_bin_args}
        COMMENT "Packaging launcher HBC app zip"
        VERBATIM)
    add_dependencies(package wii)
endif()
