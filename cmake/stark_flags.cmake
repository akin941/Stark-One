# Strict warnings on our own code only (TESTING.md §3, STARK-0100): every
# component under components/, apps/ and main/ of the repository at
# STARK_ROOT — never a vendor component (selected by directory: a name
# pattern would also catch ESP-IDF's own app_update). ESP-IDF already makes
# warnings errors, so these fail the build. Included after project() by every
# firmware project: the production one (root CMakeLists.txt) and the
# emulator storage test image (test/emu/storage_image, ADR-0018), so both
# compile our sources identically; the test image's own medium substitute
# lives under test/ and gets the same flags through STARK_EXTRA_STRICT_DIRS.
#
# -Wundef is deliberately absent: ESP-IDF's public headers test unset
# CONFIG_ symbols with #if, so it fires ~1000 times inside vendor headers our
# sources include (evidence: TESTING.md §3). The host build keeps -Wundef for
# every pure core.
set(STARK_STRICT_WARNINGS -Wshadow -Wconversion -Wdouble-promotion)
idf_build_get_property(stark_build_components BUILD_COMPONENTS)
foreach(stark_comp IN LISTS stark_build_components)
    idf_component_get_property(stark_dir ${stark_comp} COMPONENT_DIR)
    set(stark_ours FALSE)
    if(stark_dir MATCHES "^${STARK_ROOT}/(components|apps|main)(/|$)")
        set(stark_ours TRUE)
    endif()
    foreach(stark_extra IN LISTS STARK_EXTRA_STRICT_DIRS)
        if(stark_dir STREQUAL stark_extra)
            set(stark_ours TRUE)
        endif()
    endforeach()
    if(stark_ours)
        idf_component_get_property(stark_lib ${stark_comp} COMPONENT_LIB)
        get_target_property(stark_lib_type ${stark_lib} TYPE)
        if(NOT stark_lib_type STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${stark_lib} PRIVATE ${STARK_STRICT_WARNINGS})
        endif()
    endif()
endforeach()
