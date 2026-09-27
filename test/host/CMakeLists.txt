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
set(UI_PORT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_display.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_event.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/ui_port_misc.c
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
)
set(UI_PORT_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/port/include
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
)
add_executable(run_ui_launcher_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test_ui_launcher.c
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_launcher_tests BEFORE PRIVATE ${UI_PORT_INCLUDES})
target_compile_options(run_ui_launcher_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_launcher_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_launcher_tests PRIVATE unity)

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
    ${UI_PORT_SOURCES}
)
target_include_directories(run_ui_dialog_tests BEFORE PRIVATE ${UI_PORT_INCLUDES})
target_compile_options(run_ui_dialog_tests PRIVATE ${OUR_C_FLAGS})
target_link_options(run_ui_dialog_tests PRIVATE ${OUR_SANITIZER_FLAGS} ${OUR_COVERAGE_FLAGS})
target_link_libraries(run_ui_dialog_tests PRIVATE unity)

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
add_test(NAME stark_ui_format_tests COMMAND run_ui_format_tests)

# The gcovr report itself is generated by scripts/test_host.sh --coverage
# (invoking gcovr directly) once --coverage has produced .gcda/.gcno files
# via OUR_COVERAGE_FLAGS above; no separate CMake target needed.