# Nintendont loader (the libogc GUI). Links libogc + the vendored extlibs and
# fatfs-ppc, embeds the data blobs produced by the ppc/arm groups, and is
# converted to the HBC boot.dol. Was loader/Makefile (which used wii_rules).

file(GLOB _loader_srcs CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/unzip/*.c")

add_executable(loader ${_loader_srcs})
set_target_properties(loader PROPERTIES OUTPUT_NAME loader SUFFIX ".elf")

option(SUSAMUNE_MINIMAL_FREETYPE
    "Register only the FreeType modules used by the embedded TrueType font" ON)
set(SUSAMUNE_LOADER_OPTIMIZATION "-Os" CACHE STRING
    "PPC loader optimization (default -Os; -O3 is the speed override)")
set_property(CACHE SUSAMUNE_LOADER_OPTIMIZATION PROPERTY STRINGS "-O3" "-Os")
if(NOT SUSAMUNE_LOADER_OPTIMIZATION STREQUAL "-O3" AND
        NOT SUSAMUNE_LOADER_OPTIMIZATION STREQUAL "-Os")
    message(FATAL_ERROR
        "SUSAMUNE_LOADER_OPTIMIZATION must be -O3 or -Os")
endif()

target_compile_options(loader PRIVATE
    -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
    ${SUSAMUNE_LOADER_OPTIMIZATION} -g -std=gnu89 -Wall
    -ffunction-sections -fdata-sections
    -fno-unwind-tables -fno-asynchronous-unwind-tables)
target_compile_definitions(loader PRIVATE
    DEBUG DEBUG_MODULE_PATCH ARCH_IS_BIG_ENDIAN NOUNCRYPT
    SUSAMUNE_MINIMAL_FREETYPE=$<BOOL:${SUSAMUNE_MINIMAL_FREETYPE}>)

target_include_directories(loader PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../fatfs"
    "${CMAKE_CURRENT_SOURCE_DIR}/extlibs/include"
    "${LAUNCHER_GEN_DIR}"
    "${LIBOGC_INC}")

# Data blobs embedded into the loader (bin2o equivalent). The committed font
# comes from the source data/ dir; the five generated bins come from the
# ppc/arm groups' shared data dir. The stock background is drawn procedurally;
# only a user-supplied theme needs a full-size texture.
susamune_embed_binary(loader "${CMAKE_CURRENT_SOURCE_DIR}/data/font.zip"       font_zip)
susamune_embed_binary(loader "${LAUNCHER_DATA_DIR}/PADReadGC.bin"    PADReadGC_bin)
susamune_embed_binary(loader "${LAUNCHER_DATA_DIR}/kernel.zip"       kernel_zip)
susamune_embed_binary(loader "${LAUNCHER_DATA_DIR}/kernelboot.bin"   kernelboot_bin)
susamune_embed_binary(loader "${LAUNCHER_DATA_DIR}/multidol_ldr.bin" multidol_ldr_bin)
susamune_embed_binary(loader "${LAUNCHER_DATA_DIR}/stub.bin"         stub_bin)

target_link_directories(loader PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/extlibs/lib"
    "${LAUNCHER_LIB_DIR}"
    "${LIBOGC_LIB}")
target_link_libraries(loader PRIVATE
    freetype pngu png z
    asnd mad wiiuse wiidrc wupc bte fatfs-ppc ogc di m)

target_link_options(loader PRIVATE
    -g -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
    "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/loader.map,-wrap,wiiuse_register,--gc-sections")

susamune_elf2dol("${LAUNCHER_BOOT_DOL}" "$<TARGET_FILE:loader>")
add_custom_target(boot_dol ALL DEPENDS "${LAUNCHER_BOOT_DOL}")
add_dependencies(boot_dol loader)
