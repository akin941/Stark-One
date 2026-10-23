cmake_minimum_required(VERSION 3.22.1)

# Apps are components too: apps/app_<name>/ (ARCHITECTURE.md §3, §11).
set(EXTRA_COMPONENT_DIRS apps)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stark-one)

# Strict warnings on components/, apps/ and main/ (TESTING.md §3); shared
# with the emulator storage test image.
set(STARK_ROOT ${CMAKE_SOURCE_DIR})
include(${STARK_ROOT}/cmake/stark_flags.cmake)
