# Host unit-test CMake project
# Standalone, no ESP-IDF. Compiles pure core sources with Unity.
# C11, -Wall -Wextra -Werror, sanitizers enabled by default.

cmake_minimum_required(VERSION 3.22.1)
project(stark_one_host_tests LANGUAGES C)

# --- C standard -------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Emit compile_commands.json so AC #2 ("no ESP-IDF include paths") can be
# verified against the actual compiler invocations, not just a directory grep.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Unity test framework (FetchContent, pinned tag) ------------------------
include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.5.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(unity)

# --- Our strict flags (list, not string) ------------------------------------
set(OUR_WARNING_FLAGS
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wconversion
    -Wundef
    -Wdouble-promotion
    -Wswitch-enum
    -Werror=switch-enum
    -fdiagnostics-color=always
)

set(OUR_SANITIZER_FLAGS
    -fsanitize=address,undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)

if(STARK_COVERAGE)
    set(OUR_COVERAGE_FLAGS --coverage)
else()
    set(OUR_COVERAGE_FLAGS "")
endif()

# Combine as list
set(OUR_C_FLAGS
    ${OUR_WARNING_FLAGS}
    ${OUR_SANITIZER_FLAGS}
    ${OUR_COVERAGE_FLAGS}
)

# --- Include directories ----------------------------------------------------
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_err/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_board/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_hal/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_event/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_input/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_ui/include
)

# --- Sources ----------------------------------------------------------------
set(OUR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_err/stark_err.c
)

# --- Test executable --------------------------------------------------------
add_executable(run_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_err.c
    ${OUR_SOURCES}
)

# Apply strict flags as list to our test executable only.
# --coverage must be on the link line too (it pulls in the profiling
# runtime) or the link fails with undefined __llvm_gcov_* / __gcov_*
# symbols once STARK_COVERAGE is on.
target_compile_options(run_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})

# Link Unity
target_link_libraries(run_tests PRIVATE unity)

# --- stark_board_validate() tests --------------------------------------------
# Only stark_board.c (the pure, board-agnostic validation logic) — not
# board_devkitc1.c, which pulls in ESP-IDF drivers and is not host-testable.
add_executable(run_board_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_board.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_err/stark_err.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_board/stark_board.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/stark_board_host.c
)
target_compile_options(run_board_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_board_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_board_tests PRIVATE unity)

# --- stark_hal fake (host) tests ---------------------------------------------
# support/stark_hal_host.c implements components/stark_hal/include/stark_hal.h
# exactly — the same header stark_hal_esp.c implements (STARK-0007 AC #1).
add_executable(run_hal_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_err/stark_err.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/stark_hal_host.c
)
target_compile_options(run_hal_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_hal_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_hal_tests PRIVATE unity)

# --- key GPIO levels -> raw bitmap (input_sample.c) ---------------------------
# The one piece of the input path the target emulator cannot drive: pin order
# and active-low polarity, against the fake HAL (docs/VALIDATION.md).
add_executable(run_input_sample_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_input_sample.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_err/stark_err.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_input/input_sample.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/stark_hal_host.c
)
target_include_directories(run_input_sample_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_input)
target_compile_options(run_input_sample_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_input_sample_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_input_sample_tests PRIVATE unity)

# --- stark_event core tests (STARK-0009) --------------------------------------
# Only the pure core; stark_err.c is not needed (the core never calls
# stark_err_str()).
add_executable(run_event_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_event.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_event/stark_event_core.c
)
target_compile_options(run_event_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_event_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_event_tests PRIVATE unity)

# --- stark_input core tests (STARK-0011) --------------------------------------
add_executable(run_input_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_input_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_input/input_core.c
)
target_compile_options(run_input_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_input_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_input_tests PRIVATE unity)

# --- stark_gfx core tests (STARK-0013) ----------------------------------------
# gfx_internal.h (the shared clip helper) is private to the component, so
# its directory is added for this target only.
add_executable(run_gfx_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_gfx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/ppm_dump.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/gfx_surface.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/gfx_draw.c
)
target_include_directories(run_gfx_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx)
target_compile_options(run_gfx_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_gfx_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_gfx_tests PRIVATE unity)

# --- stark_gfx text tests (STARK-0014) ----------------------------------------
add_executable(run_text_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_text.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/ppm_dump.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/gfx_surface.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/gfx_draw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/gfx_font.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/fonts/font_mono16.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/fonts/font_mono10.c
)
target_compile_options(run_text_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_text_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_text_tests PRIVATE unity)

# --- stark_display band-walk tests (STARK-0016) -------------------------------
# Only the pure band arithmetic; display_bands.h is component-private.
# --- text wrapping (STARK-0102) ------------------------------------------------
add_executable(run_wrap_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_wrap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/ppm_dump.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/gfx_surface.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/gfx_draw.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/gfx_font.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx/fonts/font_mono16.c
)
target_include_directories(run_wrap_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_gfx)
target_compile_options(run_wrap_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_wrap_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_wrap_tests PRIVATE unity)

add_executable(run_display_band_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_display_bands.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_display/display_bands.c
)
target_include_directories(run_display_band_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_display)
target_compile_options(run_display_band_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_display_band_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_display_band_tests PRIVATE unity)

# --- stark_ui menu model tests (STARK-0018) -----------------------------------
add_executable(run_menu_model_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_menu_model.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_ui/ui_menu_model.c
)
target_compile_options(run_menu_model_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_menu_model_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_menu_model_tests PRIVATE unity)

# --- stark_app registry helper tests (STARK-0019) ------------------------------
# app_internal.h is component-private; stark_app.h pulls in stark_ui.h,
# which is host-includable.
add_executable(run_app_catalog_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_app_catalog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_app/app_catalog.c
)
target_include_directories(run_app_catalog_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_app
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_app/include)
target_compile_options(run_app_catalog_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_app_catalog_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_app_catalog_tests PRIVATE unity)

# --- stark_ui format helper tests (STARK-0020) --------------------------------
add_executable(run_ui_format_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_format.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_ui/ui_format.c
)
target_compile_options(run_ui_format_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_format_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_format_tests PRIVATE unity)

# --- Enable testing ---------------------------------------------------------
enable_testing()
# --- UI damage set (STARK-0101) ------------------------------------------------
add_executable(run_ui_damage_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_damage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../components/stark_ui/ui_damage.c
)
target_compile_options(run_ui_damage_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_damage_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_damage_tests PRIVATE unity)

# --- Host UI test port (Tier 4B, docs/VALIDATION.md) -------------------------
# Production stark_ui / stark_app / stark_gfx sources on test backends of
# stark_display, stark_event, stark_buzzer and esp_log (test/host/port/). The
# shim headers in port/include (esp_log.h, sdkconfig.h) are seen only by this
# target — the pure-core tests above keep building with no ESP-IDF-shaped
# header on their include path (TESTING.md §2 rule 1).
set(STARK_COMPONENTS ${CMAKE_CURRENT_SOURCE_DIR}/../../components)
# The IR codec core for apps on the UI port, without coverage (the codec
# suites own that figure).
add_library(ir_core_nocov OBJECT ${STARK_COMPONENTS}/stark_ir/ir_timing.c
    ${STARK_COMPONENTS}/stark_ir/ir_nec.c ${STARK_COMPONENTS}/stark_ir/ir_codec.c
    ${STARK_COMPONENTS}/stark_ir/ir_biphase.c ${STARK_COMPONENTS}/stark_ir/ir_rc5.c
    ${STARK_COMPONENTS}/stark_ir/ir_rc6.c ${STARK_COMPONENTS}/stark_ir/ir_sirc.c
    ${STARK_COMPONENTS}/stark_ir/ir_raw.c ${STARK_COMPONENTS}/stark_ir/ir_names.c
    ${STARK_COMPONENTS}/stark_ir/ir_tx_guard.c)
target_include_directories(ir_core_nocov PRIVATE ${STARK_COMPONENTS}/stark_ir
    ${STARK_COMPONENTS}/stark_ir/include)
target_compile_options(ir_core_nocov PRIVATE ${OUR_WARNING_FLAGS} ${OUR_SANITIZER_FLAGS})

set(UI_PORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_event.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_misc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_worker.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/stark_hal_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/ppm_dump.c
    ${STARK_COMPONENTS}/stark_err/stark_err.c
    ${STARK_COMPONENTS}/stark_event/stark_event_core.c
    ${STARK_COMPONENTS}/stark_gfx/gfx_surface.c
    ${STARK_COMPONENTS}/stark_gfx/gfx_draw.c
    ${STARK_COMPONENTS}/stark_gfx/gfx_font.c
    ${STARK_COMPONENTS}/stark_gfx/fonts/font_mono16.c
    ${STARK_COMPONENTS}/stark_gfx/fonts/font_mono10.c
    ${STARK_COMPONENTS}/stark_display/display_bands.c
    ${STARK_COMPONENTS}/stark_ui/ui_stack.c
    ${STARK_COMPONENTS}/stark_ui/ui_damage.c
    ${STARK_COMPONENTS}/stark_ui/ui_dialog.c
    ${STARK_COMPONENTS}/stark_ui/ui_dialog_layout.c
    ${STARK_COMPONENTS}/stark_ui/ui_statusbar.c
    ${STARK_COMPONENTS}/stark_ui/ui_menu.c
    ${STARK_COMPONENTS}/stark_ui/ui_menu_model.c
    ${STARK_COMPONENTS}/stark_app/app_manager.c
    ${STARK_COMPONENTS}/stark_app/app_catalog.c
    ${STARK_COMPONENTS}/stark_app/app_fault.c
)
set(UI_PORT_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/port/include
    ${STARK_COMPONENTS}/stark_ir/include
    ${STARK_COMPONENTS}/stark_module/include
    ${CMAKE_CURRENT_SOURCE_DIR}/port
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${STARK_COMPONENTS}/stark_log/include
    ${STARK_COMPONENTS}/stark_ui
    ${STARK_COMPONENTS}/stark_ui/include
    ${STARK_COMPONENTS}/stark_app
    ${STARK_COMPONENTS}/stark_app/include
    ${STARK_COMPONENTS}/stark_display
    ${STARK_COMPONENTS}/stark_display/include
    ${STARK_COMPONENTS}/stark_buzzer/include
    ${STARK_COMPONENTS}/stark_event/include
    ${STARK_COMPONENTS}/stark_input/include
    ${STARK_COMPONENTS}/stark_diag/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest
)
add_executable(run_ui_launcher_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_launcher.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_launcher_tests BEFORE PRIVATE ${UI_PORT_INCLUDES})
target_compile_options(run_ui_launcher_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_launcher_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_launcher_tests PRIVATE unity)

# The real inspector on the UI port (STARK-0505) with a fake stark_module.
add_executable(run_ui_modules_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_modules.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_modules/app_modules.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_modules/modules_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_module.c
    ${STARK_COMPONENTS}/stark_module/module_desc.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_modules_tests BEFORE PRIVATE ${UI_PORT_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_modules)
target_compile_options(run_ui_modules_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_modules_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_modules_tests PRIVATE unity)

# caps_required on the UI port (STARK-0502).
add_executable(run_ui_app_caps_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_app_caps.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_app_caps_tests BEFORE PRIVATE ${UI_PORT_INCLUDES})
target_compile_options(run_ui_app_caps_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_app_caps_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_app_caps_tests PRIVATE unity)

# The real Display Test screen on the UI port (STARK-0102).
add_executable(run_ui_displaytest_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_displaytest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_displaytest/app_displaytest.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_displaytest_tests BEFORE PRIVATE ${UI_PORT_INCLUDES})
target_compile_options(run_ui_displaytest_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_displaytest_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_displaytest_tests PRIVATE unity)

# The real V0 app screens on the UI port: frame goldens (STARK-0103).
add_executable(run_ui_apps_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_apps.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_inputtest/app_inputtest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_buzzertest/app_buzzertest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_displaytest/app_displaytest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest_ir.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_ir.c
    $<TARGET_OBJECTS:ir_core_nocov>
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest_worker.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_apps_tests BEFORE PRIVATE ${UI_PORT_INCLUDES})
target_compile_options(run_ui_apps_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_apps_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_apps_tests PRIVATE unity)

# The dialog's pure layout (STARK-0105).
add_executable(run_dialog_layout_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_dialog_layout.c
    ${STARK_COMPONENTS}/stark_ui/ui_dialog_layout.c
    ${STARK_COMPONENTS}/stark_gfx/gfx_surface.c
    ${STARK_COMPONENTS}/stark_gfx/gfx_draw.c
    ${STARK_COMPONENTS}/stark_gfx/gfx_font.c
    ${STARK_COMPONENTS}/stark_gfx/fonts/font_mono16.c
    ${STARK_COMPONENTS}/stark_gfx/fonts/font_mono10.c
)
target_include_directories(run_dialog_layout_tests PRIVATE ${STARK_COMPONENTS}/stark_ui)
target_compile_options(run_dialog_layout_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_dialog_layout_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_dialog_layout_tests PRIVATE unity)

# The dialog on the UI port, through the real App Test (STARK-0105).
add_executable(run_ui_dialog_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_dialog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest_ir.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_ir.c
    $<TARGET_OBJECTS:ir_core_nocov>
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest_worker.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_dialog_tests BEFORE PRIVATE ${UI_PORT_INCLUDES})
target_compile_options(run_ui_dialog_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_dialog_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_dialog_tests PRIVATE unity)

# Global navigation on the UI port (STARK-0106).
add_executable(run_ui_navigation_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_navigation.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest_ir.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_ir.c
    $<TARGET_OBJECTS:ir_core_nocov>
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest_worker.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_navigation_tests BEFORE PRIVATE ${UI_PORT_INCLUDES})
target_compile_options(run_ui_navigation_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_navigation_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_navigation_tests PRIVATE unity)

# The fault generation filter (STARK-0108).
add_executable(run_app_fault_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_app_fault.c
    ${STARK_COMPONENTS}/stark_app/app_fault.c
)
target_include_directories(run_app_fault_tests PRIVATE ${STARK_COMPONENTS}/stark_app
    ${STARK_COMPONENTS}/stark_app/include ${STARK_COMPONENTS}/stark_ui/include)
target_compile_options(run_app_fault_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_app_fault_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_app_fault_tests PRIVATE unity)

# Fault containment on the UI port, through the real App Test (STARK-0108).
add_executable(run_ui_fault_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_fault.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest_ir.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_ir.c
    $<TARGET_OBJECTS:ir_core_nocov>
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_apptest/app_apptest_worker.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_fault_tests BEFORE PRIVATE ${UI_PORT_INCLUDES})
target_compile_options(run_ui_fault_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_fault_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_fault_tests PRIVATE unity)

# stark_diag's pure core (STARK-0109).
add_executable(run_diag_core_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_diag_core.c
    ${STARK_COMPONENTS}/stark_diag/diag_core.c
)
target_include_directories(run_diag_core_tests PRIVATE ${STARK_COMPONENTS}/stark_diag
    ${STARK_COMPONENTS}/stark_diag/include)
target_compile_options(run_diag_core_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_diag_core_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_diag_core_tests PRIVATE unity)

# stark_storage's pure mount state machine (STARK-0201). Built without
# ESP-IDF on the include path, which is what keeps storage_core.c pure.
add_executable(run_storage_core_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_storage_core.c
    ${STARK_COMPONENTS}/stark_storage/storage_core.c
)
target_include_directories(run_storage_core_tests PRIVATE ${STARK_COMPONENTS}/stark_storage
    ${STARK_COMPONENTS}/stark_storage/include ${STARK_COMPONENTS}/stark_storage_medium/include)
target_compile_options(run_storage_core_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_storage_core_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_storage_core_tests PRIVATE unity)

# Storage on the UI port (STARK-0202): the status-bar glyph, the composition
# root's storage bridge and the real Storage app, with a settable stark_storage.
# stark_settings' pure core over a fake NVS, and the buzzer volume table (STARK-0204).
add_executable(run_settings_core_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_settings_core.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/fake_nvs.c
    ${STARK_COMPONENTS}/stark_settings/settings_core.c
)
target_include_directories(run_settings_core_tests PRIVATE ${STARK_COMPONENTS}/stark_settings
    ${STARK_COMPONENTS}/stark_settings/include)
target_compile_options(run_settings_core_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_settings_core_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_settings_core_tests PRIVATE unity)

add_executable(run_buzzer_volume_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_buzzer_volume.c
    ${STARK_COMPONENTS}/stark_buzzer/buzzer_volume.c
)
target_include_directories(run_buzzer_volume_tests PRIVATE ${STARK_COMPONENTS}/stark_buzzer)
target_compile_options(run_buzzer_volume_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_buzzer_volume_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_buzzer_volume_tests PRIVATE unity)

# The real Settings app on the UI port (STARK-0205), over the real settings
# core (built without coverage here: its own suite owns that figure).
add_library(settings_core_nocov OBJECT ${STARK_COMPONENTS}/stark_settings/settings_core.c)
target_include_directories(settings_core_nocov PRIVATE ${STARK_COMPONENTS}/stark_settings
    ${STARK_COMPONENTS}/stark_settings/include)
target_compile_options(settings_core_nocov PRIVATE ${OUR_WARNING_FLAGS} ${OUR_SANITIZER_FLAGS})
add_executable(run_ui_settings_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_settings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_settings/app_settings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_settings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/fake_nvs.c
    $<TARGET_OBJECTS:settings_core_nocov>
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_settings_tests BEFORE PRIVATE ${UI_PORT_INCLUDES}
    ${STARK_COMPONENTS}/stark_settings ${STARK_COMPONENTS}/stark_settings/include)
target_compile_options(run_ui_settings_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_settings_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_settings_tests PRIVATE unity)

# The log capture ring, the rotation plan and the log file writer (STARK-0206).
add_executable(run_log_ring_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_log_ring.c
    ${STARK_COMPONENTS}/stark_log/log_ring.c
)
target_include_directories(run_log_ring_tests PRIVATE ${STARK_COMPONENTS}/stark_log)
target_compile_options(run_log_ring_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_log_ring_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_log_ring_tests PRIVATE unity)

add_executable(run_log_rotate_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_log_rotate.c
    ${STARK_COMPONENTS}/stark_storage/log_rotate.c
)
target_include_directories(run_log_rotate_tests PRIVATE ${STARK_COMPONENTS}/stark_storage)
target_compile_options(run_log_rotate_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_log_rotate_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_log_rotate_tests PRIVATE unity)

# stark_ir's pure codec core (STARK-0301) against spec-derived vectors
# (scripts/gen_ir_vectors.py, ADR-0019 §3).
set(IR_CORE_SOURCES
    ${STARK_COMPONENTS}/stark_ir/ir_timing.c
    ${STARK_COMPONENTS}/stark_ir/ir_nec.c
    ${STARK_COMPONENTS}/stark_ir/ir_codec.c
    ${STARK_COMPONENTS}/stark_ir/ir_biphase.c
    ${STARK_COMPONENTS}/stark_ir/ir_rc5.c
    ${STARK_COMPONENTS}/stark_ir/ir_rc6.c
    ${STARK_COMPONENTS}/stark_ir/ir_sirc.c
    ${STARK_COMPONENTS}/stark_ir/ir_raw.c
    ${STARK_COMPONENTS}/stark_ir/ir_symbols.c
    ${STARK_COMPONENTS}/stark_ir/ir_capture.c
    ${STARK_COMPONENTS}/stark_ir/ir_tx_guard.c
    ${STARK_COMPONENTS}/stark_ir/ir_names.c
)
set(IR_INCLUDES ${STARK_COMPONENTS}/stark_ir ${STARK_COMPONENTS}/stark_ir/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vectors)
foreach(ir_test ir_timing ir_nec ir_codecs ir_raw ir_symbols ir_tx_guard ir_names)
    add_executable(run_${ir_test}_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_${ir_test}.c
        ${IR_CORE_SOURCES})
    target_include_directories(run_${ir_test}_tests PRIVATE ${IR_INCLUDES})
    target_compile_options(run_${ir_test}_tests PRIVATE ${OUR_C_FLAGS})
    target_link_options(run_${ir_test}_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
    target_link_libraries(run_${ir_test}_tests PRIVATE unity)
    add_test(NAME stark_${ir_test}_tests COMMAND run_${ir_test}_tests)
endforeach()

# stark_nfc's pure core (STARK-0401…) against spec-derived vectors
# (scripts/gen_nfc_vectors.py, ADR-0020 §4).
set(NFC_CORE_SOURCES
    ${STARK_COMPONENTS}/stark_nfc/ndef.c
    ${STARK_COMPONENTS}/stark_nfc/pn532_frame.c
    ${STARK_COMPONENTS}/stark_nfc/pn532.c
    ${STARK_COMPONENTS}/stark_nfc/nfc_ident.c
    ${STARK_COMPONENTS}/stark_nfc/nfc_type2.c
    ${STARK_COMPONENTS}/stark_nfc/nfc_reader.c
    ${STARK_COMPONENTS}/stark_nfc/nfc_poll.c
    ${STARK_COMPONENTS}/stark_nfc/nfc_writer.c
    ${STARK_COMPONENTS}/stark_nfc/nfc_dump.c
)
# The test-only fake PN532 (ADR-0020 §3), shared with the emulator test image.
set(FAKE_PN532 ${CMAKE_CURRENT_SOURCE_DIR}/../fake/fake_pn532.c)
set(NFC_INCLUDES ${STARK_COMPONENTS}/stark_nfc ${STARK_COMPONENTS}/stark_nfc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/vectors ${CMAKE_CURRENT_SOURCE_DIR}/../fake)
foreach(nfc_test ndef pn532_frame pn532 nfc_ident nfc_type2 nfc_reader nfc_poll nfc_writer
        nfc_dump)
    add_executable(run_${nfc_test}_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_${nfc_test}.c
        ${NFC_CORE_SOURCES} ${FAKE_PN532})
    target_include_directories(run_${nfc_test}_tests PRIVATE ${NFC_INCLUDES})
    target_compile_options(run_${nfc_test}_tests PRIVATE ${OUR_C_FLAGS})
    target_link_options(run_${nfc_test}_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
    target_link_libraries(run_${nfc_test}_tests PRIVATE unity)
    add_test(NAME stark_${nfc_test}_tests COMMAND run_${nfc_test}_tests)
endforeach()

# storage_core.c is linked only for stark_storage_state_str(); this copy is
# built without coverage so the state machine's own suite owns its figure.
add_library(storage_core_nocov OBJECT ${STARK_COMPONENTS}/stark_storage/storage_core.c)
target_include_directories(storage_core_nocov PRIVATE ${STARK_COMPONENTS}/stark_storage/include
    ${STARK_COMPONENTS}/stark_storage_medium/include)
target_compile_options(storage_core_nocov PRIVATE ${OUR_WARNING_FLAGS} ${OUR_SANITIZER_FLAGS})
add_library(storage_path_nocov OBJECT ${STARK_COMPONENTS}/stark_storage/storage_path.c)
target_include_directories(storage_path_nocov PRIVATE ${STARK_COMPONENTS}/stark_storage/include
    ${STARK_COMPONENTS}/stark_storage_medium/include)
target_compile_options(storage_path_nocov PRIVATE ${OUR_WARNING_FLAGS} ${OUR_SANITIZER_FLAGS})
# stark_storage's paths (pure) and file helpers (STARK-0203): the real POSIX
# I/O ops in a temp directory, and a fault-injecting wrapper around them.
add_executable(run_storage_path_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_storage_path.c
    ${STARK_COMPONENTS}/stark_storage/storage_path.c
)
target_include_directories(run_storage_path_tests PRIVATE ${STARK_COMPONENTS}/stark_storage
    ${STARK_COMPONENTS}/stark_storage/include ${STARK_COMPONENTS}/stark_storage_medium/include)
target_compile_options(run_storage_path_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_storage_path_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_storage_path_tests PRIVATE unity)

add_executable(run_storage_file_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_storage_file.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/fault_io.c
    ${STARK_COMPONENTS}/stark_storage/storage_file.c
    ${STARK_COMPONENTS}/stark_storage/storage_io_posix.c
    $<TARGET_OBJECTS:storage_path_nocov>
)
target_include_directories(run_storage_file_tests PRIVATE ${STARK_COMPONENTS}/stark_storage
    ${STARK_COMPONENTS}/stark_storage/include ${STARK_COMPONENTS}/stark_storage_medium/include)
target_compile_options(run_storage_file_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_storage_file_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_storage_file_tests PRIVATE unity)

add_library(storage_file_nocov OBJECT ${STARK_COMPONENTS}/stark_storage/storage_file.c
    ${STARK_COMPONENTS}/stark_storage/storage_io_posix.c ${STARK_COMPONENTS}/stark_storage/log_rotate.c)
target_include_directories(storage_file_nocov PRIVATE ${STARK_COMPONENTS}/stark_storage
    ${STARK_COMPONENTS}/stark_storage/include ${STARK_COMPONENTS}/stark_storage_medium/include)
target_compile_options(storage_file_nocov PRIVATE ${OUR_WARNING_FLAGS} ${OUR_SANITIZER_FLAGS})
add_executable(run_storage_log_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_storage_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/fault_io.c
    ${STARK_COMPONENTS}/stark_storage/storage_log.c
    $<TARGET_OBJECTS:storage_file_nocov>
    $<TARGET_OBJECTS:storage_path_nocov>
)
target_include_directories(run_storage_log_tests PRIVATE ${STARK_COMPONENTS}/stark_storage
    ${STARK_COMPONENTS}/stark_storage/include ${STARK_COMPONENTS}/stark_storage_medium/include)
target_compile_options(run_storage_log_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_storage_log_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_storage_log_tests PRIVATE unity)

add_executable(run_ui_storage_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_storage/app_storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_storage/storage_wtest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main/storage_ui.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_storage.c
    $<TARGET_OBJECTS:storage_core_nocov>
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_storage_tests BEFORE PRIVATE ${UI_PORT_INCLUDES}
    ${STARK_COMPONENTS}/stark_storage/include ${STARK_COMPONENTS}/stark_storage_medium/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_storage)
target_compile_options(run_ui_storage_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_storage_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_storage_tests PRIVATE unity)

# NDEF and tag names for apps on the UI port, without coverage (the core suites
# own that figure).
add_library(nfc_core_nocov OBJECT ${STARK_COMPONENTS}/stark_nfc/ndef.c
    ${STARK_COMPONENTS}/stark_nfc/nfc_ident.c ${STARK_COMPONENTS}/stark_nfc/nfc_dump.c)
target_include_directories(nfc_core_nocov PRIVATE ${STARK_COMPONENTS}/stark_nfc
    ${STARK_COMPONENTS}/stark_nfc/include)
target_compile_options(nfc_core_nocov PRIVATE ${OUR_WARNING_FLAGS} ${OUR_SANITIZER_FLAGS})

# The module descriptor (STARK-0501, ADR-0021) and the test-only fake 24C02.
set(FAKE_24C02 ${CMAKE_CURRENT_SOURCE_DIR}/../fake/fake_24c02.c)
set(MODULE_INCLUDES ${STARK_COMPONENTS}/stark_module ${STARK_COMPONENTS}/stark_module/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../fake)
foreach(mod_test module_desc fake_24c02 module_core module_claim)
    add_executable(run_${mod_test}_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_${mod_test}.c
        ${STARK_COMPONENTS}/stark_module/module_desc.c
        ${STARK_COMPONENTS}/stark_module/module_core.c ${FAKE_24C02})
    target_include_directories(run_${mod_test}_tests PRIVATE ${MODULE_INCLUDES})
    target_compile_options(run_${mod_test}_tests PRIVATE ${OUR_C_FLAGS})
    target_link_options(run_${mod_test}_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
    target_link_libraries(run_${mod_test}_tests PRIVATE unity)
    add_test(NAME stark_${mod_test}_tests COMMAND run_${mod_test}_tests)
endforeach()

# stark_subghz's pure cores (STARK-0601, ADR-0022): the register profiles, the CC1101
# driver and the module service, against the test-only fake CC1101 (shared with the
# emulator test image) and, for the service, the real module core over the fake 24C02.
set(SUBGHZ_CORE_SOURCES
    ${STARK_COMPONENTS}/stark_subghz/cc1101_regs.c
    ${STARK_COMPONENTS}/stark_subghz/cc1101_drv.c
    ${STARK_COMPONENTS}/stark_subghz/subghz_rssi.c
    ${STARK_COMPONENTS}/stark_subghz/subghz_raw.c
    ${STARK_COMPONENTS}/stark_subghz/subghz_decode.c
    ${STARK_COMPONENTS}/stark_subghz/subghz_txpolicy.c
    ${STARK_COMPONENTS}/stark_subghz/subghz_svc.c
)
set(FAKE_CC1101 ${CMAKE_CURRENT_SOURCE_DIR}/../fake/fake_cc1101.c)
set(SUBGHZ_INCLUDES ${STARK_COMPONENTS}/stark_subghz ${STARK_COMPONENTS}/stark_subghz/include
    ${STARK_COMPONENTS}/stark_subghz_transport/include ${MODULE_INCLUDES})
foreach(sg_test cc1101_regs cc1101_drv fake_cc1101 subghz_service subghz_rssi subghz_measure
        subghz_raw subghz_capture subghz_decode subghz_txpolicy subghz_tx)
    add_executable(run_${sg_test}_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_${sg_test}.c
        ${SUBGHZ_CORE_SOURCES} ${FAKE_CC1101}
        ${STARK_COMPONENTS}/stark_module/module_desc.c
        ${STARK_COMPONENTS}/stark_module/module_core.c ${FAKE_24C02})
    target_include_directories(run_${sg_test}_tests PRIVATE ${SUBGHZ_INCLUDES})
    target_compile_options(run_${sg_test}_tests PRIVATE ${OUR_C_FLAGS})
    target_link_options(run_${sg_test}_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
    target_link_libraries(run_${sg_test}_tests PRIVATE unity)
    add_test(NAME stark_${sg_test}_tests COMMAND run_${sg_test}_tests)
endforeach()

# The lab's pin safety (STARK-0701, ADR-0023): the board's pin classes, the test-only pad model,
# the pure pad decisions over the real module core and fake 24C02, and the production transport
# over the fake HAL.
set(FAKE_LAB ${CMAKE_CURRENT_SOURCE_DIR}/../fake/fake_lab.c)
set(LAB_INCLUDES ${STARK_COMPONENTS}/stark_lab ${STARK_COMPONENTS}/stark_lab/include
    ${STARK_COMPONENTS}/stark_lab_transport/include ${MODULE_INCLUDES})
add_executable(run_board_pins_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_board_pins.c
    ${STARK_COMPONENTS}/stark_err/stark_err.c ${STARK_COMPONENTS}/stark_board/stark_board.c)
target_compile_options(run_board_pins_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_board_pins_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_board_pins_tests PRIVATE unity)
add_test(NAME stark_board_pins_tests COMMAND run_board_pins_tests)

add_executable(run_fake_lab_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_fake_lab.c ${FAKE_LAB}
    ${STARK_COMPONENTS}/stark_err/stark_err.c)
target_include_directories(run_fake_lab_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../fake)
target_compile_options(run_fake_lab_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_fake_lab_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_fake_lab_tests PRIVATE unity)
add_test(NAME stark_fake_lab_tests COMMAND run_fake_lab_tests)

add_executable(run_lab_pins_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_lab_pins.c
    ${STARK_COMPONENTS}/stark_lab/lab_pins.c ${STARK_COMPONENTS}/stark_lab/lab_pwm.c
    ${STARK_COMPONENTS}/stark_lab/lab_meas.c
    ${FAKE_LAB} ${FAKE_24C02}
    ${STARK_COMPONENTS}/stark_err/stark_err.c ${STARK_COMPONENTS}/stark_board/stark_board.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/stark_board_host.c
    ${STARK_COMPONENTS}/stark_module/module_desc.c ${STARK_COMPONENTS}/stark_module/module_core.c)
target_include_directories(run_lab_pins_tests PRIVATE ${LAB_INCLUDES})
target_compile_options(run_lab_pins_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_lab_pins_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_lab_pins_tests PRIVATE unity)
add_test(NAME stark_lab_pins_tests COMMAND run_lab_pins_tests)

# The frequency and duty arithmetic (STARK-0703, ADR-0023 §4) against the edge sets of
# scripts/gen_lab_vectors.py, an independent Python implementation.
add_executable(run_lab_meas_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_lab_meas.c
    ${STARK_COMPONENTS}/stark_lab/lab_meas.c)
target_include_directories(run_lab_meas_tests PRIVATE ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../fake)
target_compile_options(run_lab_meas_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_lab_meas_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_lab_meas_tests PRIVATE unity)
add_test(NAME stark_lab_meas_tests COMMAND run_lab_meas_tests)

add_executable(run_lab_pwm_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_lab_pwm.c
    ${STARK_COMPONENTS}/stark_lab/lab_pwm.c)
target_include_directories(run_lab_pwm_tests PRIVATE ${LAB_INCLUDES})
target_compile_options(run_lab_pwm_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_lab_pwm_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_lab_pwm_tests PRIVATE unity)
add_test(NAME stark_lab_pwm_tests COMMAND run_lab_pwm_tests)

add_executable(run_lab_service_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_lab_service.c
    ${STARK_COMPONENTS}/stark_lab_transport/lab_transport_hal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/stark_hal_host.c
    ${STARK_COMPONENTS}/stark_err/stark_err.c ${STARK_COMPONENTS}/stark_board/stark_board.c)
target_include_directories(run_lab_service_tests PRIVATE ${LAB_INCLUDES})
target_compile_options(run_lab_service_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_lab_service_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_lab_service_tests PRIVATE unity)
add_test(NAME stark_lab_service_tests COMMAND run_lab_service_tests)

# The I2C scanner's rules (STARK-0704, ADR-0023 §5): lab_i2c.c and the hints over the test-only bus,
# behind the host shim of the transport's I2C group (the production header's guard), against the
# vectors of scripts/gen_lab_vectors.py.
add_executable(run_lab_i2c_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_lab_i2c.c
    ${STARK_COMPONENTS}/stark_lab/lab_i2c.c ${STARK_COMPONENTS}/stark_lab/lab_i2c_hints.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/lab_transport_i2c_host.c
    ${FAKE_LAB} ${FAKE_24C02} ${STARK_COMPONENTS}/stark_err/stark_err.c)
target_include_directories(run_lab_i2c_tests PRIVATE ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../fake)
target_compile_options(run_lab_i2c_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_lab_i2c_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_lab_i2c_tests PRIVATE unity)
add_test(NAME stark_lab_i2c_tests COMMAND run_lab_i2c_tests)

# The UART tool (STARK-0705, ADR-0023 §7): its data rules and auto-baud against the vectors of
# scripts/gen_lab_vectors.py, and the session over the host shim of the transport's UART group and
# the test-only peer, with the real pin hold of lab_pins.c.
add_executable(run_lab_uart_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_lab_uart.c
    ${STARK_COMPONENTS}/stark_lab/lab_uart.c ${FAKE_LAB} ${STARK_COMPONENTS}/stark_err/stark_err.c)
target_include_directories(run_lab_uart_tests PRIVATE ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../fake)
target_compile_options(run_lab_uart_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_lab_uart_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_lab_uart_tests PRIVATE unity)
add_test(NAME stark_lab_uart_tests COMMAND run_lab_uart_tests)

add_executable(run_lab_autobaud_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_lab_autobaud.c
    ${STARK_COMPONENTS}/stark_lab/lab_autobaud.c ${STARK_COMPONENTS}/stark_lab/lab_uart.c
    ${FAKE_LAB} ${STARK_COMPONENTS}/stark_err/stark_err.c)
target_include_directories(run_lab_autobaud_tests PRIVATE ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../fake)
target_compile_options(run_lab_autobaud_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_lab_autobaud_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_lab_autobaud_tests PRIVATE unity)
add_test(NAME stark_lab_autobaud_tests COMMAND run_lab_autobaud_tests)

add_executable(run_lab_uart_svc_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_lab_uart_svc.c
    ${STARK_COMPONENTS}/stark_lab/lab_uart_svc.c ${STARK_COMPONENTS}/stark_lab/lab_uart.c
    ${STARK_COMPONENTS}/stark_lab/lab_autobaud.c ${STARK_COMPONENTS}/stark_lab/lab_pins.c
    ${STARK_COMPONENTS}/stark_lab/lab_pwm.c ${STARK_COMPONENTS}/stark_lab/lab_meas.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/lab_transport_uart_host.c ${FAKE_LAB}
    ${STARK_COMPONENTS}/stark_err/stark_err.c ${STARK_COMPONENTS}/stark_board/stark_board.c)
target_include_directories(run_lab_uart_svc_tests PRIVATE ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../fake)
target_compile_options(run_lab_uart_svc_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_lab_uart_svc_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_lab_uart_svc_tests PRIVATE unity)
add_test(NAME stark_lab_uart_svc_tests COMMAND run_lab_uart_svc_tests)

# The UART app (STARK-0705, ADR-0023 §7): its pure rows and texts, its card log over an in-memory
# stark_storage, and the real app on the UI port over the real session, the pin hold and the test-only
# peer.
add_executable(run_uart_views_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_uart_views.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_uart/uart_views.c
    ${STARK_COMPONENTS}/stark_lab/lab_uart.c ${STARK_COMPONENTS}/stark_err/stark_err.c)
target_include_directories(run_uart_views_tests PRIVATE ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_uart)
target_compile_options(run_uart_views_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_uart_views_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_uart_views_tests PRIVATE unity)
add_test(NAME stark_uart_views_tests COMMAND run_uart_views_tests)

add_executable(run_uart_log_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_uart_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_uart/uart_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/storage_fake.c
    ${STARK_COMPONENTS}/stark_lab/lab_uart.c ${STARK_COMPONENTS}/stark_err/stark_err.c)
target_include_directories(run_uart_log_tests BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/port/include ${STARK_COMPONENTS}/stark_log/include ${LAB_INCLUDES}
    ${STARK_COMPONENTS}/stark_storage/include ${STARK_COMPONENTS}/stark_storage_medium/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_uart)
target_compile_options(run_uart_log_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_uart_log_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_uart_log_tests PRIVATE unity)
add_test(NAME stark_uart_log_tests COMMAND run_uart_log_tests)

add_executable(run_ui_uart_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_uart/app_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_uart/uart_views.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_uart/uart_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_uart.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/lab_transport_uart_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/storage_fake.c
    ${STARK_COMPONENTS}/stark_lab/lab_uart.c
    ${STARK_COMPONENTS}/stark_lab/lab_autobaud.c
    ${STARK_COMPONENTS}/stark_lab/lab_uart_svc.c
    ${STARK_COMPONENTS}/stark_lab/lab_pins.c
    ${STARK_COMPONENTS}/stark_lab/lab_pwm.c
    ${STARK_COMPONENTS}/stark_lab/lab_meas.c
    ${STARK_COMPONENTS}/stark_board/stark_board.c
    ${FAKE_LAB}
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_uart_tests BEFORE PRIVATE ${UI_PORT_INCLUDES} ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../fake ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_uart
    ${STARK_COMPONENTS}/stark_storage/include ${STARK_COMPONENTS}/stark_storage_medium/include)
target_compile_options(run_ui_uart_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_uart_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_uart_tests PRIVATE unity)
add_test(NAME stark_ui_uart_tests COMMAND run_ui_uart_tests)

# The SPI probe (STARK-0706, ADR-0023 §6): its rules over the real module core and fake 24C02, the
# host shim of the transport's SPI group (the production header's guard), the test-only SPI devices
# and the fake CC1101 (which must see nothing), against the vectors of scripts/gen_lab_vectors.py.
add_executable(run_lab_spi_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_lab_spi.c
    ${STARK_COMPONENTS}/stark_lab/lab_spi.c ${STARK_COMPONENTS}/stark_lab/lab_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/lab_transport_spi_host.c
    ${STARK_COMPONENTS}/stark_module/module_desc.c ${STARK_COMPONENTS}/stark_module/module_core.c
    ${FAKE_LAB} ${FAKE_24C02} ${FAKE_CC1101} ${STARK_COMPONENTS}/stark_err/stark_err.c)
target_include_directories(run_lab_spi_tests PRIVATE ${LAB_INCLUDES}
    ${STARK_COMPONENTS}/stark_subghz_transport/include ${CMAKE_CURRENT_SOURCE_DIR}/../fake)
target_compile_options(run_lab_spi_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_lab_spi_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_lab_spi_tests PRIVATE unity)
add_test(NAME stark_lab_spi_tests COMMAND run_lab_spi_tests)

# The I2C app (STARK-0704, ADR-0023 §5): its pure editor rows on their own.
add_executable(run_i2c_views_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_i2c_views.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_i2c/i2c_views.c
    ${STARK_COMPONENTS}/stark_lab/lab_i2c.c ${STARK_COMPONENTS}/stark_err/stark_err.c)
target_include_directories(run_i2c_views_tests PRIVATE ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_i2c)
target_compile_options(run_i2c_views_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_i2c_views_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_i2c_views_tests PRIVATE unity)
add_test(NAME stark_i2c_views_tests COMMAND run_i2c_views_tests)

# The real I2C app on the UI port (STARK-0704) over the real rules, the transport guard and the
# test-only bus.
add_executable(run_ui_i2c_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_i2c/app_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_i2c/i2c_views.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_i2c.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/lab_transport_i2c_host.c
    ${STARK_COMPONENTS}/stark_lab/lab_i2c.c
    ${STARK_COMPONENTS}/stark_lab/lab_i2c_hints.c
    ${FAKE_LAB} ${FAKE_24C02}
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_i2c_tests BEFORE PRIVATE ${UI_PORT_INCLUDES} ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../fake ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_i2c)
target_compile_options(run_ui_i2c_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_i2c_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_i2c_tests PRIVATE unity)
add_test(NAME stark_ui_i2c_tests COMMAND run_ui_i2c_tests)

# The SPI app (STARK-0706, ADR-0023 §6): its pure setup rows on their own, and the real app on the
# UI port over the real rules, the real module core, the transport guard and the test-only devices.
add_executable(run_spi_views_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_spi_views.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_spi/spi_views.c
    ${STARK_COMPONENTS}/stark_lab/lab_spi.c ${STARK_COMPONENTS}/stark_lab/lab_i2c.c
    ${STARK_COMPONENTS}/stark_err/stark_err.c)
target_include_directories(run_spi_views_tests PRIVATE ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_spi)
target_compile_options(run_spi_views_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_spi_views_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_spi_views_tests PRIVATE unity)
add_test(NAME stark_spi_views_tests COMMAND run_spi_views_tests)

add_executable(run_ui_spi_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_spi/app_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_spi/spi_views.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/lab_transport_spi_host.c
    ${STARK_COMPONENTS}/stark_lab/lab_spi.c
    ${STARK_COMPONENTS}/stark_lab/lab_i2c.c
    ${STARK_COMPONENTS}/stark_module/module_desc.c
    ${STARK_COMPONENTS}/stark_module/module_core.c
    ${FAKE_LAB} ${FAKE_24C02}
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_spi_tests BEFORE PRIVATE ${UI_PORT_INCLUDES} ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../fake ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_spi)
target_compile_options(run_ui_spi_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_spi_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_spi_tests PRIVATE unity)
add_test(NAME stark_ui_spi_tests COMMAND run_ui_spi_tests)

# The real NFC app on the UI port (STARK-0404) with a fake stark_nfc.
add_executable(run_ui_nfc_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_nfc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_nfc/app_nfc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_nfc/nfc_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_nfc/nfc_write.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_nfc/nfc_save.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_nfc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_storage.c
    $<TARGET_OBJECTS:nfc_core_nocov>
    $<TARGET_OBJECTS:storage_core_nocov>
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_nfc_tests BEFORE PRIVATE ${UI_PORT_INCLUDES}
    ${STARK_COMPONENTS}/stark_nfc/include ${STARK_COMPONENTS}/stark_storage/include
    ${STARK_COMPONENTS}/stark_storage_medium/include ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_nfc)
target_compile_options(run_ui_nfc_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_nfc_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_nfc_tests PRIVATE unity)

# The real Sub-GHz app on the UI port (STARK-0602/0603) with a fake stark_subghz and the
# in-memory card. The pure RSSI and raw-capture helpers are linked without coverage
# (test_subghz_rssi and test_subghz_raw own those figures).
add_library(subghz_rssi_nocov OBJECT ${STARK_COMPONENTS}/stark_subghz/subghz_rssi.c)
target_include_directories(subghz_rssi_nocov PRIVATE ${STARK_COMPONENTS}/stark_subghz/include
    ${STARK_COMPONENTS}/stark_subghz_transport/include ${MODULE_INCLUDES})
target_compile_options(subghz_rssi_nocov PRIVATE ${OUR_WARNING_FLAGS} ${OUR_SANITIZER_FLAGS})
add_library(subghz_raw_nocov OBJECT ${STARK_COMPONENTS}/stark_subghz/subghz_raw.c)
target_include_directories(subghz_raw_nocov PRIVATE ${STARK_COMPONENTS}/stark_subghz/include
    ${STARK_COMPONENTS}/stark_subghz_transport/include ${MODULE_INCLUDES})
target_compile_options(subghz_raw_nocov PRIVATE ${OUR_WARNING_FLAGS} ${OUR_SANITIZER_FLAGS})
add_library(subghz_decode_nocov OBJECT ${STARK_COMPONENTS}/stark_subghz/subghz_decode.c)
target_include_directories(subghz_decode_nocov PRIVATE ${STARK_COMPONENTS}/stark_subghz/include
    ${STARK_COMPONENTS}/stark_subghz_transport/include ${MODULE_INCLUDES})
target_compile_options(subghz_decode_nocov PRIVATE ${OUR_WARNING_FLAGS} ${OUR_SANITIZER_FLAGS})
add_library(subghz_txpolicy_nocov OBJECT ${STARK_COMPONENTS}/stark_subghz/subghz_txpolicy.c)
target_include_directories(subghz_txpolicy_nocov PRIVATE ${STARK_COMPONENTS}/stark_subghz/include
    ${STARK_COMPONENTS}/stark_subghz_transport/include ${MODULE_INCLUDES})
target_compile_options(subghz_txpolicy_nocov PRIVATE ${OUR_WARNING_FLAGS} ${OUR_SANITIZER_FLAGS})
add_executable(run_ui_subghz_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_subghz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_subghz/app_subghz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_subghz/subghz_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_subghz/subghz_files.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_subghz/subghz_send.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_subghz.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_storage.c
    $<TARGET_OBJECTS:subghz_rssi_nocov>
    $<TARGET_OBJECTS:subghz_raw_nocov>
    $<TARGET_OBJECTS:subghz_decode_nocov>
    $<TARGET_OBJECTS:subghz_txpolicy_nocov>
    $<TARGET_OBJECTS:storage_core_nocov>
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_subghz_tests BEFORE PRIVATE ${UI_PORT_INCLUDES}
    ${STARK_COMPONENTS}/stark_subghz/include ${STARK_COMPONENTS}/stark_subghz_transport/include
    ${MODULE_INCLUDES} ${STARK_COMPONENTS}/stark_storage/include
    ${STARK_COMPONENTS}/stark_storage_medium/include ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_subghz)
target_compile_options(run_ui_subghz_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_subghz_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_subghz_tests PRIVATE unity)

# The real IR app on the UI port (STARK-0305/0306) with fake stark_ir / stark_storage.
add_executable(run_ui_ir_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_ir.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_ir/app_ir.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_ir/ir_save.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_ir/ir_browse.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_ir/ir_view.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_ir/ir_send.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_ir.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_storage.c
    $<TARGET_OBJECTS:ir_core_nocov>
    $<TARGET_OBJECTS:storage_core_nocov>
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_ir_tests BEFORE PRIVATE ${UI_PORT_INCLUDES}
    ${STARK_COMPONENTS}/stark_storage/include ${STARK_COMPONENTS}/stark_storage_medium/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_ir)
target_compile_options(run_ui_ir_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_ir_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_ir_tests PRIVATE unity)

# The real Diagnostics screen on the UI port, with a settable fake stark_diag.
add_executable(run_ui_diagnostics_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_diagnostics.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_diagnostics/app_diagnostics.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_diag.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_diagnostics_tests BEFORE PRIVATE ${UI_PORT_INCLUDES})
target_compile_options(run_ui_diagnostics_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_diagnostics_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_diagnostics_tests PRIVATE unity)

# The self-test (STARK-0707, ADR-0023 section 8): the runner, the report formatter, the root's checks
# and export over a model of the machine (test/host/support/selftest_env_fake.c), the app's pure
# rows and walk, and the real app on the UI port over the real service.
set(SELFTEST_INCLUDES ${STARK_COMPONENTS}/stark_diag ${STARK_COMPONENTS}/stark_diag/include
    ${STARK_COMPONENTS}/stark_module/include ${STARK_COMPONENTS}/stark_settings
    ${STARK_COMPONENTS}/stark_settings/include ${STARK_COMPONENTS}/stark_storage/include
    ${STARK_COMPONENTS}/stark_storage_medium/include ${STARK_COMPONENTS}/stark_input/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_selftest)
set(SELFTEST_CORE ${STARK_COMPONENTS}/stark_diag/selftest_core.c)
set(SELFTEST_REPORT ${STARK_COMPONENTS}/stark_diag/selftest_report.c)
set(SELFTEST_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../main)
set(SELFTEST_FAKE ${CMAKE_CURRENT_SOURCE_DIR}/support/selftest_env_fake.c
    ${STARK_COMPONENTS}/stark_settings/settings_core.c ${STARK_COMPONENTS}/stark_err/stark_err.c)
function(selftest_test name)
    add_executable(run_${name}_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_${name}.c ${ARGN})
    target_include_directories(run_${name}_tests PRIVATE ${SELFTEST_INCLUDES})
    target_compile_options(run_${name}_tests PRIVATE ${OUR_C_FLAGS})
    target_link_options(run_${name}_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
    target_link_libraries(run_${name}_tests PRIVATE unity)
    add_test(NAME stark_${name}_tests COMMAND run_${name}_tests)
endfunction()
selftest_test(selftest_core ${SELFTEST_CORE})
selftest_test(selftest_report ${SELFTEST_REPORT} ${SELFTEST_CORE})
selftest_test(selftest_checks ${SELFTEST_ROOT}/selftest_checks.c ${SELFTEST_ROOT}/selftest_sha256.c
    ${SELFTEST_FAKE})
selftest_test(selftest_export ${SELFTEST_ROOT}/selftest_export.c ${SELFTEST_REPORT} ${SELFTEST_CORE}
    ${SELFTEST_FAKE})
selftest_test(selftest_views ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_selftest/selftest_views.c)

add_executable(run_ui_selftest_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_selftest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_selftest/app_selftest.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_selftest/selftest_views.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_selftest.c
    ${SELFTEST_CORE}
    ${STARK_COMPONENTS}/stark_diag/selftest_svc.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_selftest_tests BEFORE PRIVATE ${UI_PORT_INCLUDES}
    ${STARK_COMPONENTS}/stark_diag ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_selftest)
target_compile_options(run_ui_selftest_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_selftest_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_selftest_tests PRIVATE unity)
add_test(NAME stark_ui_selftest_tests COMMAND run_ui_selftest_tests)

add_test(NAME stark_host_tests COMMAND run_tests)
add_test(NAME stark_board_tests COMMAND run_board_tests)
add_test(NAME stark_hal_tests COMMAND run_hal_tests)
add_test(NAME stark_event_tests COMMAND run_event_tests)
add_test(NAME stark_input_tests COMMAND run_input_tests)
add_test(NAME stark_input_sample_tests COMMAND run_input_sample_tests)
add_test(NAME stark_gfx_tests COMMAND run_gfx_tests)
add_test(NAME stark_text_tests COMMAND run_text_tests)
add_test(NAME stark_wrap_tests COMMAND run_wrap_tests)
add_test(NAME stark_display_band_tests COMMAND run_display_band_tests)
add_test(NAME stark_menu_model_tests COMMAND run_menu_model_tests)
add_test(NAME stark_ui_damage_tests COMMAND run_ui_damage_tests)
add_test(NAME stark_app_catalog_tests COMMAND run_app_catalog_tests)
add_test(NAME stark_ui_launcher_tests COMMAND run_ui_launcher_tests)
add_test(NAME stark_ui_displaytest_tests COMMAND run_ui_displaytest_tests)
add_test(NAME stark_ui_apps_tests COMMAND run_ui_apps_tests)
add_test(NAME stark_dialog_layout_tests COMMAND run_dialog_layout_tests)
add_test(NAME stark_ui_dialog_tests COMMAND run_ui_dialog_tests)
add_test(NAME stark_ui_navigation_tests COMMAND run_ui_navigation_tests)
add_test(NAME stark_app_fault_tests COMMAND run_app_fault_tests)
add_test(NAME stark_ui_fault_tests COMMAND run_ui_fault_tests)
add_test(NAME stark_diag_core_tests COMMAND run_diag_core_tests)
add_test(NAME stark_storage_core_tests COMMAND run_storage_core_tests)
add_test(NAME stark_settings_core_tests COMMAND run_settings_core_tests)
add_test(NAME stark_buzzer_volume_tests COMMAND run_buzzer_volume_tests)
add_test(NAME stark_storage_path_tests COMMAND run_storage_path_tests)
add_test(NAME stark_storage_file_tests COMMAND run_storage_file_tests)
add_test(NAME stark_storage_log_tests COMMAND run_storage_log_tests)
add_test(NAME stark_log_ring_tests COMMAND run_log_ring_tests)
add_test(NAME stark_log_rotate_tests COMMAND run_log_rotate_tests)
add_test(NAME stark_ui_storage_tests COMMAND run_ui_storage_tests)
add_test(NAME stark_ui_settings_tests COMMAND run_ui_settings_tests)
add_test(NAME stark_ui_ir_tests COMMAND run_ui_ir_tests)
add_test(NAME stark_ui_nfc_tests COMMAND run_ui_nfc_tests)
add_test(NAME stark_ui_subghz_tests COMMAND run_ui_subghz_tests)

# The GPIO app (STARK-0702, ADR-0023): its pure pin view on its own, and the real app on the UI
# port over the real pad decisions (lab_pins.c, lab_pwm.c) and the test-only pad model.
add_executable(run_gpio_views_tests ${CMAKE_CURRENT_SOURCE_DIR}/test_gpio_views.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_gpio/gpio_views.c
    ${STARK_COMPONENTS}/stark_lab/lab_pwm.c)
target_include_directories(run_gpio_views_tests PRIVATE ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_gpio)
target_compile_options(run_gpio_views_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_gpio_views_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_gpio_views_tests PRIVATE unity)
add_test(NAME stark_gpio_views_tests COMMAND run_gpio_views_tests)

add_executable(run_ui_gpio_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_gpio/app_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_gpio/gpio_views.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_gpio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_module.c
    ${CMAKE_CURRENT_SOURCE_DIR}/support/stark_board_host.c
    ${STARK_COMPONENTS}/stark_lab/lab_pins.c
    ${STARK_COMPONENTS}/stark_lab/lab_pwm.c
    ${STARK_COMPONENTS}/stark_lab/lab_meas.c
    ${STARK_COMPONENTS}/stark_board/stark_board.c
    ${FAKE_LAB}
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_gpio_tests BEFORE PRIVATE ${UI_PORT_INCLUDES} ${LAB_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../fake ${CMAKE_CURRENT_SOURCE_DIR}/../../apps/app_gpio)
target_compile_options(run_ui_gpio_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_gpio_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_gpio_tests PRIVATE unity)
add_test(NAME stark_ui_gpio_tests COMMAND run_ui_gpio_tests)
add_test(NAME stark_ui_app_caps_tests COMMAND run_ui_app_caps_tests)
add_test(NAME stark_ui_modules_tests COMMAND run_ui_modules_tests)
add_test(NAME stark_ui_diagnostics_tests COMMAND run_ui_diagnostics_tests)
add_test(NAME stark_ui_format_tests COMMAND run_ui_format_tests)

# The gcovr report itself is generated by scripts/test_host.sh --coverage
# (invoking gcovr directly) once --coverage has produced .gcda/.gcno files
# via OUR_COVERAGE_FLAGS above; no separate CMake target needed.