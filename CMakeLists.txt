cmake_minimum_required(VERSION 3.21)

# --- Toolchain -------------------------------------------------------------
# The Kuribo clang fork must be selected before project() so CMake uses it as
# the compiler. Its default target isn't hosted, so the usual compiler check
# (which links a test executable) can't succeed; we assert it works instead.
# CMAKE_SYSTEM_NAME=Generic marks this as a bare-metal cross build, which keeps
# CMake off the host-Windows toolchain path (no RC compiler, no MSVC probing).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

if(NOT DEFINED KURIBO_COMPILER_HOME)
    if(DEFINED ENV{KURIBO_COMPILER_HOME})
        set(KURIBO_COMPILER_HOME "$ENV{KURIBO_COMPILER_HOME}")
    else()
        set(KURIBO_COMPILER_HOME "${CMAKE_CURRENT_SOURCE_DIR}/toolchain")
    endif()
endif()
set(KURIBO_COMPILER_HOME "${KURIBO_COMPILER_HOME}" CACHE PATH "Kuribo clang toolchain directory")

if(CMAKE_HOST_WIN32)
    set(_kuribo_exe ".exe")
endif()
set(CMAKE_CXX_COMPILER "${KURIBO_COMPILER_HOME}/clang++${_kuribo_exe}")
set(CMAKE_CXX_COMPILER_WORKS TRUE)

project(susamune CXX)

# Header dependency tracking. The compiler is forced above and its check is
# skipped, so CMake never identifies it and leaves CMAKE_DEPFILE_FLAGS_CXX
# empty -- the Ninja rule asks for a depfile that the compile command is then
# never told to write, and *editing a header rebuilds nothing*. Spell the flags
# out (the fork is clang, so GCC-style).
set(CMAKE_DEPFILE_FLAGS_CXX "-MD -MT <DEP_TARGET> -MF <DEP_FILE>")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR "The Visual Studio generator is not supported. Configure with Ninja, e.g. `cmake -B build -G Ninja` or `cmake --preset default`.")
endif()

set(VENV_DIR "${CMAKE_SOURCE_DIR}/venv")
set(SUSAMUNE_PYTHON "" CACHE FILEPATH
    "Explicit Python runtime for Susamune build scripts")
if(SUSAMUNE_PYTHON)
    set(VENV_PYTHON "${SUSAMUNE_PYTHON}")
elseif(CMAKE_HOST_WIN32)
    set(VENV_PYTHON "${VENV_DIR}/Scripts/python.exe")
else()
    set(VENV_PYTHON "${VENV_DIR}/bin/python")
endif()
if(NOT EXISTS "${VENV_PYTHON}")
    message(FATAL_ERROR "Virtualenv not found at ${VENV_PYTHON}. Run `python setup_venv.py` first.")
endif()

# --- Options ---------------------------------------------------------------

# VERS is a list: one mod_<vers>.bin is built per entry and all of them are
# packaged into the single, version-agnostic launcher. The ISO/DOL pipeline
# (Dolphin development) can only target one disc at a time, so it follows the
# first entry -- VERS_PRIMARY below.
set(VERS "jp" CACHE STRING "Game versions to build mod binaries for (list: jp;us;pal)")
set(SUPPORTED_VERS jp us pal)
if(NOT VERS)
    message(FATAL_ERROR "VERS must name at least one of: ${SUPPORTED_VERS}")
endif()
foreach(_v IN LISTS VERS)
    if(NOT _v IN_LIST SUPPORTED_VERS)
        message(FATAL_ERROR "VERS entry '${_v}' is not one of: ${SUPPORTED_VERS}")
    endif()
endforeach()
list(GET VERS 0 VERS_PRIMARY)
string(TOUPPER "${VERS_PRIMARY}" VERS_PRIMARY_UPPER)

set(LAUNCHER_DEFAULT_SOURCE "di" CACHE STRING "Where to load the iso from by default in meta.xml")
set_property(CACHE LAUNCHER_DEFAULT_SOURCE PROPERTY STRINGS di sd usb)
if(LAUNCHER_VERSION STREQUAL "V1.2.0" OR
   LAUNCHER_VERSION STREQUAL "V2.1.1" OR
   LAUNCHER_VERSION STREQUAL "V2.0.0 \"Ghosts of Delfino\"" OR
   LAUNCHER_VERSION STREQUAL "V2.0.1 \"Ghosts of Delfino\"" OR
   LAUNCHER_VERSION STREQUAL "V2.0.2 \"Ghosts of Delfino\"" OR
   LAUNCHER_VERSION STREQUAL "V2.0.3 \"Ghosts of Delfino\"" OR
   LAUNCHER_VERSION MATCHES "^V2\\.0\\.0 Gate [0-9]+" OR
   LAUNCHER_VERSION MATCHES "^V2\\.0\\.0 .*Pre-release [0-9]+$" OR
   LAUNCHER_VERSION MATCHES "^V2\\.0\\.0 .*Release Candidate [0-9]+$" OR
   LAUNCHER_VERSION MATCHES "^V2\\.1\\.0 Pre-Release [0-9]+" OR
   LAUNCHER_VERSION MATCHES "^V2\\.1\\.0 Release Candidate [0-9]+" OR
   LAUNCHER_VERSION MATCHES "^V2\\.2\\.0 Pre-Release [0-9]+")
    unset(LAUNCHER_VERSION CACHE)
endif()
set(LAUNCHER_VERSION "V2.2.0 Pre-Release 2 \"The House Always Wins\"" CACHE STRING
    "Version shown in the packaged launcher meta.xml")

option(IS_EMULATOR "Target Dolphin (ON) instead of Wii/Nintendont (OFF)" OFF)
option(ENABLE_SAVESTATE_DBG "Enable savestate debug output" OFF)
option(ENABLE_MEM_DIAGNOSTICS "Enable the MEM1 safety overlay and canaries" OFF)
option(ENABLE_DEBUG_WARPS "Enable debug warp menu tabs and stage warping" OFF)
option(UPDATE_ISO_METADATA "Update ISO metadata (game code 01 -> 02, change picture, name, etc.)" OFF)
option(LAUNCHER_DISABLE_SUSAMUNE_PATCH "Disable the Susamune patch in the launcher kernel" OFF)
set(SUSAMUNE_LOADER_OPTIMIZATION "-Os" CACHE STRING
    "PPC loader optimization (default -Os; -O3 is the speed override)")
set_property(CACHE SUSAMUNE_LOADER_OPTIMIZATION PROPERTY STRINGS "-O3" "-Os")

set(ISO_CODE_jp GMSJ01)
set(ISO_CODE_us GMSE01)
set(ISO_CODE_pal GMSP01)
set(ISO_CODE "${ISO_CODE_${VERS_PRIMARY}}")

if(DEFINED ENV{SMS_ISO})
    set(_sms_iso_default "$ENV{SMS_ISO}")
else()
    set(_sms_iso_default "${CMAKE_SOURCE_DIR}/${ISO_CODE}.iso")
endif()
set(SMS_ISO "${_sms_iso_default}" CACHE FILEPATH "Source game ISO to patch")

set(LD_SCRIPT "${CMAKE_SOURCE_DIR}/maps/${VERS_PRIMARY}.ld")
set(MAP_FILE "${CMAKE_SOURCE_DIR}/maps/${VERS_PRIMARY}.map")

# --- Mod object ------------------------------------------------------------
# Compile the sources and partial-link (-r) them into a single relocatable
# object that the DOL patcher / bin packer later injects. One object per game
# version: the sources differ only by the SUSAMUNE_VERSION_* define, but that
# selects a whole MEM1 layout (addresses.hxx), so the versions cannot share a
# build directory.

file(GLOB MOD_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
if(NOT ENABLE_MEM_DIAGNOSTICS)
    list(REMOVE_ITEM MOD_SOURCES "${CMAKE_SOURCE_DIR}/src/mem_diagnostics.cpp")
endif()
if(NOT ENABLE_DEBUG_WARPS)
    list(REMOVE_ITEM MOD_SOURCES "${CMAKE_SOURCE_DIR}/src/debug_warp.cpp")
endif()

set(SHARED_FLAGS
    --target=powerpc-gecko-ibm-kuribo-eabi
    -Oz
    -Xclang
    -fmerge-functions
    -fno-exceptions
    -fno-rtti
    # `s8` in include/ is a typedef for plain `char`, which is unsigned on this
    # target; the game was built by CodeWarrior with `-char signed` (see the
    # decomp's configure.py), so its structs mean the signed byte.
    -fsigned-char
    -ffast-math
    -fpermissive
    -fno-unwind-tables
    -nodefaultlibs
    -nostdlib
    -fno-use-cxa-atexit
    -fno-c++-static-destructors
    -ffunction-sections
    -fdata-sections)

function(susamune_add_mod_object vers)
    string(TOUPPER "${vers}" _upper)
    add_executable(susamune_obj_${vers} ${MOD_SOURCES})
    set_target_properties(susamune_obj_${vers} PROPERTIES
        OUTPUT_NAME susamune_pre_${vers}
        SUFFIX .o
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

    target_include_directories(susamune_obj_${vers} PRIVATE
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_SOURCE_DIR}/include/JSystem")

    target_compile_options(susamune_obj_${vers} PRIVATE
        ${SHARED_FLAGS}
        -nobuiltininc
        -nostdinc++
        -std=c++17
        -Werror
        -Wno-main
        -Wno-incompatible-library-redeclaration)

    target_compile_definitions(susamune_obj_${vers} PRIVATE
        IS_EMULATOR=$<BOOL:${IS_EMULATOR}>
        ENABLE_SAVESTATE_DBG=$<BOOL:${ENABLE_SAVESTATE_DBG}>
        ENABLE_MEM_DIAGNOSTICS=$<BOOL:${ENABLE_MEM_DIAGNOSTICS}>
        ENABLE_DEBUG_WARPS=$<BOOL:${ENABLE_DEBUG_WARPS}>
        SUSAMUNE_VERSION_${_upper}=1)

    target_link_options(susamune_obj_${vers} PRIVATE ${SHARED_FLAGS} -fuse-ld=lld -Werror -r)
endfunction()

foreach(_v IN LISTS VERS)
    susamune_add_mod_object(${_v})
endforeach()

set(MOD_OBJ "$<TARGET_FILE:susamune_obj_${VERS_PRIMARY}>")

# --- Pipeline scripts (run from the venv) ----------------------------------

set(IN_ISO_DIR "${CMAKE_BINARY_DIR}/in_iso")
set(OUT_ISO_DIR "${CMAKE_BINARY_DIR}/out_iso")
set(IN_ISO_STAMP "${CMAKE_BINARY_DIR}/in_iso.stamp")
set(OUT_ISO_STAMP "${CMAKE_BINARY_DIR}/out_iso.stamp")
set(PATCHED_DOL "${CMAKE_BINARY_DIR}/main.dol")
set(GECKO_TXT "${CMAKE_BINARY_DIR}/moonshine.txt")
set(FINAL_ISO "${CMAKE_BINARY_DIR}/moonshine_${VERS_PRIMARY}.iso")
# One launcher for every disc revision -- the mod is no longer compiled into it.
set(LAUNCHER_ZIP "${CMAKE_BINARY_DIR}/moonshine_launcher.zip")

add_custom_command(
    OUTPUT "${IN_ISO_STAMP}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/extract_iso.py" "${SMS_ISO}" "${IN_ISO_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${IN_ISO_STAMP}"
    DEPENDS "${SMS_ISO}" "${CMAKE_SOURCE_DIR}/scripts/extract_iso.py"
    COMMENT "Extracting ${SMS_ISO}"
    VERBATIM)

# The pristine extracted disc copied into a working directory that Dolphin can
# folder-boot (out_iso/root/sys/main.dol + out_iso/root/files/...). The heavy
# copy only reruns when the extraction changes; the `dol` target then drops the
# freshly patched main.dol on top each build (see below). This is what the
# former SCons `pre_iso` target did, and what the .vscode launch tasks expect.
add_custom_command(
    OUTPUT "${OUT_ISO_STAMP}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${OUT_ISO_DIR}/root"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${IN_ISO_DIR}/root" "${OUT_ISO_DIR}/root"
    COMMAND "${CMAKE_COMMAND}" -E touch "${OUT_ISO_STAMP}"
    DEPENDS "${IN_ISO_STAMP}"
    COMMENT "Populating out_iso for Dolphin folder-boot"
    VERBATIM)

# One mod_<vers>.bin per requested version: the linked blob plus the hook writes
# that realise it, in the format include/susamune/mod_bin.h describes. These are
# what the launcher ships and loads at runtime -- nothing about the mod is
# compiled into the launcher any more.
set(MOD_BINS "")
foreach(_v IN LISTS VERS)
    set(_manifest "${CMAKE_BINARY_DIR}/susamune_manifest_${_v}.json")
    set(_bin "${CMAKE_BINARY_DIR}/mod_${_v}.bin")
    add_custom_command(
        OUTPUT "${_manifest}"
        COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
            --obj "$<TARGET_FILE:susamune_obj_${_v}>"
            --linker-script "${CMAKE_SOURCE_DIR}/maps/${_v}.ld"
            --kuribo-home "${KURIBO_COMPILER_HOME}"
            --vers "${_v}"
            --out "${_manifest}"
            launcher
        DEPENDS susamune_obj_${_v} "${CMAKE_SOURCE_DIR}/maps/${_v}.ld"
                "${CMAKE_SOURCE_DIR}/scripts/patches.py"
                "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
                "${CMAKE_SOURCE_DIR}/scripts/dol_c_kit/devkit_tools.py"
                "${CMAKE_SOURCE_DIR}/include/susamune/mod_bin.h"
                "${CMAKE_SOURCE_DIR}/include/susamune/mem2_map.h"
        COMMENT "Generating ${_v} mod manifest"
        VERBATIM)
    add_custom_command(
        OUTPUT "${_bin}"
        COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/gen_mod_bin.py"
            "${_manifest}" -o "${_bin}"
        DEPENDS "${_manifest}" "${CMAKE_SOURCE_DIR}/scripts/gen_mod_bin.py"
                "${CMAKE_SOURCE_DIR}/include/susamune/mod_bin.h"
                "${CMAKE_SOURCE_DIR}/include/susamune/mem2_map.h"
        COMMENT "Packing mod_${_v}.bin"
        VERBATIM)
    list(APPEND MOD_BINS "${_bin}")
endforeach()
add_custom_target(mod_bins ALL DEPENDS ${MOD_BINS})

# Release builds can emit standard BPS patches without possessing retail ISOs.
# The checked-in layouts contain only offsets and CRCs; each mod manifest
# supplies the newly linked code and concrete hook words.
if(IS_EMULATOR AND UPDATE_ISO_METADATA)
    message(STATUS "The source-free BPS keeps retail ISO metadata; use the ISO target for UPDATE_ISO_METADATA")
elseif(IS_EMULATOR)
    set(EMULATOR_BPS_FILES "")
    foreach(_v IN LISTS VERS)
        set(_layout "${CMAKE_SOURCE_DIR}/data/iso_layout_${_v}.json")
        set(_bps "${CMAKE_BINARY_DIR}/moonshine_${_v}.bps")
        if(EXISTS "${_layout}")
            add_custom_command(
                OUTPUT "${_bps}"
                COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/gen_iso_bps.py" build
                    --layout "${_layout}"
                    --mod-manifest "${CMAKE_BINARY_DIR}/susamune_manifest_${_v}.json"
                    --output "${_bps}"
                DEPENDS "${CMAKE_BINARY_DIR}/susamune_manifest_${_v}.json"
                        "${_layout}"
                        "${CMAKE_SOURCE_DIR}/scripts/gen_iso_bps.py"
                        "${CMAKE_SOURCE_DIR}/scripts/patches.py"
                COMMENT "Building source-free ${_v} emulator BPS"
                VERBATIM)
            list(APPEND EMULATOR_BPS_FILES "${_bps}")
        else()
            message(STATUS "No source-free BPS layout for ${_v}; skipping its emulator patch")
        endif()
    endforeach()
    if(EMULATOR_BPS_FILES)
        add_custom_target(emulator_patches DEPENDS ${EMULATOR_BPS_FILES})
    endif()
endif()

# Patches main.dol and installs it into out_iso so Dolphin folder-boot (and the
# .vscode tasks) always run the current build without needing the `iso` target.
add_custom_command(
    OUTPUT "${PATCHED_DOL}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
        --obj "${MOD_OBJ}"
        --linker-script "${LD_SCRIPT}"
        --kuribo-home "${KURIBO_COMPILER_HOME}"
        --vers "${VERS_PRIMARY}"
        --in-dol "${IN_ISO_DIR}/root/sys/main.dol"
        --out "${PATCHED_DOL}"
        patch_dol
    COMMAND "${CMAKE_COMMAND}" -E copy "${PATCHED_DOL}" "${OUT_ISO_DIR}/root/sys/main.dol"
    DEPENDS susamune_obj_${VERS_PRIMARY} "${IN_ISO_STAMP}" "${OUT_ISO_STAMP}" "${LD_SCRIPT}"
            "${CMAKE_SOURCE_DIR}/scripts/patches.py"
            "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
            "${CMAKE_SOURCE_DIR}/scripts/dol_c_kit/devkit_tools.py"
            "${CMAKE_SOURCE_DIR}/include/susamune/mod_bin.h"
            "${CMAKE_SOURCE_DIR}/include/susamune/mem2_map.h"
    COMMENT "Patching main.dol"
    VERBATIM)
add_custom_target(dol DEPENDS "${PATCHED_DOL}")

# NOTE: gecko code target is currently broken, it doesn't seem to work in-game. But it is left here anyway.
add_custom_command(
    OUTPUT "${GECKO_TXT}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
        --obj "${MOD_OBJ}"
        --linker-script "${LD_SCRIPT}"
        --kuribo-home "${KURIBO_COMPILER_HOME}"
        --vers "${VERS_PRIMARY}"
        --out "${GECKO_TXT}"
        gecko
    DEPENDS susamune_obj_${VERS_PRIMARY} "${LD_SCRIPT}"
            "${CMAKE_SOURCE_DIR}/scripts/patches.py"
            "${CMAKE_SOURCE_DIR}/scripts/link_mod.py"
            "${CMAKE_SOURCE_DIR}/scripts/dol_c_kit/devkit_tools.py"
            "${CMAKE_SOURCE_DIR}/include/susamune/mod_bin.h"
            "${CMAKE_SOURCE_DIR}/include/susamune/mem2_map.h"
    COMMENT "Generating Gecko code list"
    VERBATIM)
add_custom_target(gecko DEPENDS "${GECKO_TXT}")

add_custom_command(
    OUTPUT "${FINAL_ISO}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/build_iso.py"
        --update-metadata "${UPDATE_ISO_METADATA}"
        --in-iso "${IN_ISO_DIR}"
        --out-iso "${OUT_ISO_DIR}"
        --vers "${VERS_PRIMARY}"
        --dol "${PATCHED_DOL}"
        --output "${FINAL_ISO}"
    DEPENDS "${PATCHED_DOL}" "${IN_ISO_STAMP}"
            "${CMAKE_SOURCE_DIR}/scripts/build_iso.py"
    COMMENT "Building ${FINAL_ISO}"
    VERBATIM)
add_custom_target(iso DEPENDS "${FINAL_ISO}")

# The launcher (custom Nintendont) is a CMake super-build under launcher/. It's a
# separate build tree because it spans three cross toolchains (devkitARM,
# devkitPPC, devkitPPC+libogc) that CMake can't mix with the Kuribo clang used
# here. We drive it as an ExternalProject.
#
# The launcher does not embed the blobs or hook writes; they remain runtime
# mod_<vers>.bin files. Its displayed build checksum does fingerprint those
# files, so their final bytes must exist before the otherwise-generic launcher.
include(ExternalProject)

string(REPLACE ";" "|" _mod_bins_arg "${MOD_BINS}")

set(BUILD_ID_HEADER
    "${CMAKE_BINARY_DIR}/launcher/shared/gen/susamune_build_id.h")
add_custom_command(
    OUTPUT "${BUILD_ID_HEADER}"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/gen_build_id.py"
        --output "${BUILD_ID_HEADER}" ${MOD_BINS}
    DEPENDS ${MOD_BINS} "${CMAKE_SOURCE_DIR}/scripts/gen_build_id.py"
    COMMENT "Generating launcher mod checksum"
    VERBATIM)
add_custom_target(launcher_build_id DEPENDS "${BUILD_ID_HEADER}")

ExternalProject_Add(nintendont_launcher
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/launcher"
    BINARY_DIR "${CMAKE_BINARY_DIR}/launcher"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    LIST_SEPARATOR |
    CMAKE_ARGS
        "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
        "-DLAUNCHER_PYTHON=${VENV_PYTHON}"
        "-DLAUNCHER_SCRIPTS_DIR=${CMAKE_SOURCE_DIR}/scripts"
        "-DSUSAMUNE_MOD_BINS=${_mod_bins_arg}"
        "-DLAUNCHER_OUTPUT_ZIP=${LAUNCHER_ZIP}"
        "-DLAUNCHER_DEFAULT_SOURCE=${LAUNCHER_DEFAULT_SOURCE}"
        "-DLAUNCHER_VERSION=${LAUNCHER_VERSION}"
        "-DLAUNCHER_TEST_LOG=${CMAKE_SOURCE_DIR}/doc/v2.2.0-pr2-testing.md"
        "-DLAUNCHER_CHANGELOG=${CMAKE_SOURCE_DIR}/doc/v2.2.0-pr2-changelog.md"
        "-DLAUNCHER_DISABLE_SUSAMUNE_PATCH=${LAUNCHER_DISABLE_SUSAMUNE_PATCH}"
        "-DSUSAMUNE_LOADER_OPTIMIZATION:STRING=${SUSAMUNE_LOADER_OPTIMIZATION}"
    DEPENDS mod_bins launcher_build_id
    BUILD_ALWAYS ON
    INSTALL_COMMAND ""
    USES_TERMINAL_CONFIGURE ON
    USES_TERMINAL_BUILD ON
    EXCLUDE_FROM_ALL TRUE)

add_custom_target(launcher DEPENDS nintendont_launcher)

# Regenerate the checked-in linker script from the CodeWarrior map. Run on
# demand (`cmake --build build --target regen_ld`) after the map changes.
add_custom_target(regen_ld
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/map_to_ld.py" "${MAP_FILE}" "${LD_SCRIPT}"
    COMMENT "Regenerating ${LD_SCRIPT} from ${MAP_FILE}"
    VERBATIM)

add_custom_target(regen_all_ld
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/map_to_ld.py"
        "${CMAKE_SOURCE_DIR}/maps/jp.map" "${CMAKE_SOURCE_DIR}/maps/jp.ld"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/map_to_ld.py"
        "${CMAKE_SOURCE_DIR}/maps/us.map" "${CMAKE_SOURCE_DIR}/maps/us.ld"
    COMMAND "${VENV_PYTHON}" "${CMAKE_SOURCE_DIR}/scripts/map_to_ld.py"
        "${CMAKE_SOURCE_DIR}/maps/pal.map" "${CMAKE_SOURCE_DIR}/maps/pal.ld"
    COMMENT "Regenerating linker scripts for all supported versions"
    VERBATIM)
