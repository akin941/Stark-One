# The emulator storage test image (ADR-0018 §3, TASKS.md STARK-0201).
#
# esp-emu models no SD card, so storage scenarios (test/emu/storage/*.toml)
# run this image instead of the production one. It builds the repository's
# own main, every app and every component — with exactly three substitutions
# from ./components: stark_storage_medium (a FAT volume on an internal flash
# partition instead of a microSD), stark_nfc_transport and stark_module_transport
# (the test-only fakes in test/fake instead of the I2C bus).
# scripts/check_emu_image.py proves in CI that nothing else differs.
cmake_minimum_required(VERSION 3.22.1)

get_filename_component(STARK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

file(GLOB stark_components LIST_DIRECTORIES true "${STARK_ROOT}/components/*")
file(GLOB stark_apps LIST_DIRECTORIES true "${STARK_ROOT}/apps/app_*")
list(FILTER stark_components EXCLUDE REGEX "/stark_storage_medium$") # substitution 1 (ADR-0018)
list(FILTER stark_components EXCLUDE REGEX "/stark_nfc_transport$")  # substitution 2 (ADR-0020)
list(FILTER stark_components EXCLUDE REGEX "/stark_module_transport$")  # substitution 3 (ADR-0021)
set(EXTRA_COMPONENT_DIRS
    ${stark_components}
    ${stark_apps}
    "${STARK_ROOT}/main"
    "${CMAKE_CURRENT_LIST_DIR}/components/stark_storage_medium"
    "${CMAKE_CURRENT_LIST_DIR}/components/stark_nfc_transport"
    "${CMAKE_CURRENT_LIST_DIR}/components/stark_module_transport"
)
# The production defaults, unchanged; the partition table is found next to
# this file (partitions.csv = production table + the test FAT partition).
set(SDKCONFIG_DEFAULTS "${STARK_ROOT}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stark-one-emu-storage)

# The FAT volume starts with the spec-derived lab signals (STARK-0306,
# ADR-0019 §5) — like a user's card that already holds captures. Generated
# by scripts/gen_ir_vectors.py; test image only.
fatfs_create_spiflash_image(emufat "${CMAKE_CURRENT_LIST_DIR}/seed" FLASH_IN_PROJECT)

set(STARK_EXTRA_STRICT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components/stark_storage_medium"
    "${CMAKE_CURRENT_LIST_DIR}/components/stark_nfc_transport"
    "${CMAKE_CURRENT_LIST_DIR}/components/stark_module_transport")
include(${STARK_ROOT}/cmake/stark_flags.cmake)
