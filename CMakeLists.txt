cmake_minimum_required(VERSION 3.22.1)

# Apps are components too: apps/app_<name>/ (ARCHITECTURE.md §3, §11).
set(EXTRA_COMPONENT_DIRS apps)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(stark-one)

# Strict warnings on our own code only (TESTING.md §3, STARK-0100): every
# component under components/, apps/ and main/ — never a vendor component
# (selected by directory: a name pattern would also catch ESP-IDF's own
# app_update). ESP-IDF already makes warnings errors, so these fail the build.
# -Wundef is deliberately absent here: ESP-IDF's public headers test unset
# CONFIG_ symbols with #if, so it fires ~1000 times inside vendor headers our
# sources include (evidence: TESTING.md §3). The host build keeps -Wundef for
# every pure core.
set(STARK_STRICT_WARNINGS -Wshadow -Wconversion -Wdouble-promotion)
idf_build_get_property(stark_build_components BUILD_COMPONENTS)
foreach(stark_comp IN LISTS stark_build_components)
    idf_component_get_property(stark_dir ${stark_comp} COMPONENT_DIR)
    if(stark_dir MATCHES "^${CMAKE_SOURCE_DIR}/(components|apps|main)(/|$)")
        idf_component_get_property(stark_lib ${stark_comp} COMPONENT_LIB)
        get_target_property(stark_lib_type ${stark_lib} TYPE)
        if(NOT stark_lib_type STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${stark_lib} PRIVATE ${STARK_STRICT_WARNINGS})
        endif()
    endif()
endforeach()
