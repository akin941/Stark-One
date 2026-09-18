cmake_minimum_required(VERSION 3.22.1)

# Firmware version from git describe, fallback to "dev"
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE STARK_FIRMWARE_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_RESULT
    )
    if(NOT GIT_RESULT EQUAL 0)
        set(STARK_FIRMWARE_VERSION "dev")
    endif()
else()
    set(STARK_FIRMWARE_VERSION "dev")
endif()

# Build timestamp (UTC)
string(TIMESTAMP STARK_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

# Generate version header in binary dir
get_filename_component(MAIN_DIR ${CMAKE_CURRENT_LIST_FILE} DIRECTORY)
configure_file(
    ${MAIN_DIR}/stark_version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/stark_version.h
)

idf_component_register(
    SRCS
        stark_main.c
    INCLUDE_DIRS
        "."
        ${CMAKE_CURRENT_BINARY_DIR}
    REQUIRES
        stark_err
        stark_log
        stark_board
)